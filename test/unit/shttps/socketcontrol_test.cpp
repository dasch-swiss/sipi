/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gtest/gtest.h"

#include <chrono>
#include <thread>

#include "shttps/SocketControl.h"
#include "shttps/ThreadControl.h"

namespace {

// No-op thread function — exits immediately, leaving socketpair fds valid for SocketControl
void *noop_thread(void *) { return nullptr; }

// No-op close function for tests with fake fds (C function pointer compatible)
int noop_close(const shttps::SocketControl::SocketInfo &) { return 0; }

// Helper: create a SocketControl with 2 control sockets + HTTP + optional DYN sockets
class SocketControlTest : public ::testing::Test
{
protected:
  static constexpr int kNThreads = 2;
  std::unique_ptr<shttps::ThreadControl> thread_control;
  std::unique_ptr<shttps::SocketControl> sc;

  void SetUp() override
  {
    thread_control = std::make_unique<shttps::ThreadControl>(kNThreads, noop_thread, nullptr);
    sc = std::make_unique<shttps::SocketControl>(*thread_control);
  }

  void TearDown() override
  {
    sc.reset();
    thread_control.reset();
  }

  // Create a fake DYN_SOCKET SocketInfo with a given fd number.
  // Fake fds (100, 101, ...) are used for tests that don't call get_waiting()
  // (which performs a liveness poll() that requires a real fd).
  static shttps::SocketControl::SocketInfo make_dyn_socket(int fd)
  {
    return shttps::SocketControl::SocketInfo(
      shttps::SocketControl::NOOP, shttps::SocketControl::DYN_SOCKET, fd);
  }

  // Create a real socketpair — caller must close both fds
  static std::pair<int, int> make_socketpair()
  {
    int sv[2];
    if (socketpair(PF_LOCAL, SOCK_STREAM, 0, sv) != 0) {
      return {-1, -1};
    }
    return {sv[0], sv[1]};
  }
};

// --- add_dyn_socket ---

TEST_F(SocketControlTest, AddDynSocketIncreasesSize)
{
  // Initially: 2 CONTROL_SOCKETs
  int initial_size = sc->get_sockets_size();
  EXPECT_EQ(initial_size, kNThreads);

  sc->add_dyn_socket(make_dyn_socket(100));

  EXPECT_EQ(sc->get_sockets_size(), initial_size + 1);
  EXPECT_EQ(sc->get_dyn_socket_base(), initial_size);// first dyn socket is right after control sockets
}

TEST_F(SocketControlTest, AddDynSocketAppearsInPollArray)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));

  pollfd *arr = sc->get_sockets_arr();
  int size = sc->get_sockets_size();

  // Last two entries should be our dyn sockets
  EXPECT_EQ(arr[size - 2].fd, 100);
  EXPECT_EQ(arr[size - 1].fd, 101);
}

TEST_F(SocketControlTest, AddMultipleDynSocketsSetsBaseOnce)
{
  sc->add_http_socket(50);

  int before_dyn = sc->get_sockets_size();
  sc->add_dyn_socket(make_dyn_socket(100));
  int base = sc->get_dyn_socket_base();

  sc->add_dyn_socket(make_dyn_socket(101));
  sc->add_dyn_socket(make_dyn_socket(102));

  // dyn_socket_base should not change after first add
  EXPECT_EQ(sc->get_dyn_socket_base(), base);
  EXPECT_EQ(sc->get_sockets_size(), before_dyn + 3);
}

// --- remove ---

TEST_F(SocketControlTest, RemoveDynSocketDecreasesSize)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));

  int size_before = sc->get_sockets_size();

  shttps::SocketControl::SocketInfo removed;
  sc->remove(sc->get_dyn_socket_base(), removed);

  EXPECT_EQ(sc->get_sockets_size(), size_before - 1);
  EXPECT_EQ(removed.sid, 100);
}

TEST_F(SocketControlTest, RemoveLastDynSocketPreservesOthers)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));
  sc->add_dyn_socket(make_dyn_socket(102));

  int last_idx = sc->get_sockets_size() - 1;
  shttps::SocketControl::SocketInfo removed;
  sc->remove(last_idx, removed);

  EXPECT_EQ(removed.sid, 102);

  // Remaining dyn sockets should be 100, 101
  pollfd *arr = sc->get_sockets_arr();
  int base = sc->get_dyn_socket_base();
  EXPECT_EQ(arr[base].fd, 100);
  EXPECT_EQ(arr[base + 1].fd, 101);
}

TEST_F(SocketControlTest, RemoveMiddleDynSocketShiftsSubsequent)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));
  sc->add_dyn_socket(make_dyn_socket(102));

  int base = sc->get_dyn_socket_base();

  // Remove middle (101)
  shttps::SocketControl::SocketInfo removed;
  sc->remove(base + 1, removed);
  EXPECT_EQ(removed.sid, 101);

  // After removal: 100 at base, 102 at base+1 (shifted down)
  pollfd *arr = sc->get_sockets_arr();
  EXPECT_EQ(arr[base].fd, 100);
  EXPECT_EQ(arr[base + 1].fd, 102);
}

// --- try_move_to_waiting ---

TEST_F(SocketControlTest, TryMoveToWaitingRemovesFromPollSetAndQueues)
{
  auto [fd0, fd0_peer] = make_socketpair();
  ASSERT_NE(fd0, -1);

  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(fd0));
  sc->add_dyn_socket(make_dyn_socket(101));

  int size_before = sc->get_sockets_size();
  int base = sc->get_dyn_socket_base();

  EXPECT_TRUE(sc->try_move_to_waiting(base));

  EXPECT_EQ(sc->get_sockets_size(), size_before - 1);

  // The moved socket should be retrievable from waiting queue
  shttps::SocketControl::SocketInfo waiting;
  EXPECT_TRUE(sc->get_waiting(waiting, noop_close));
  EXPECT_EQ(waiting.sid, fd0);

  close(fd0);
  close(fd0_peer);
}

TEST_F(SocketControlTest, TryMoveToWaitingMultipleSocketsPreservesFIFO)
{
  auto [fd0, fd0_peer] = make_socketpair();
  auto [fd1, fd1_peer] = make_socketpair();
  ASSERT_NE(fd0, -1);
  ASSERT_NE(fd1, -1);

  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(fd0));
  sc->add_dyn_socket(make_dyn_socket(fd1));
  sc->add_dyn_socket(make_dyn_socket(102));

  int base = sc->get_dyn_socket_base();

  // Move fd0 first, then fd1 (which is now at base after fd0 was removed)
  EXPECT_TRUE(sc->try_move_to_waiting(base));
  EXPECT_TRUE(sc->try_move_to_waiting(base));

  // FIFO: fd0 should come out first, then fd1
  shttps::SocketControl::SocketInfo w1, w2;
  EXPECT_TRUE(sc->get_waiting(w1, noop_close));
  EXPECT_EQ(w1.sid, fd0);
  EXPECT_TRUE(sc->get_waiting(w2, noop_close));
  EXPECT_EQ(w2.sid, fd1);

  close(fd0); close(fd0_peer);
  close(fd1); close(fd1_peer);
}

TEST_F(SocketControlTest, TryMoveToWaitingSucceedsWhenQueueNotFull)
{
  sc->add_http_socket(50);
  sc->set_max_waiting_connections(2);
  // Fake fds are fine here — try_move_to_waiting doesn't do liveness check
  sc->add_dyn_socket(make_dyn_socket(100));

  int base = sc->get_dyn_socket_base();
  EXPECT_TRUE(sc->try_move_to_waiting(base));
  EXPECT_EQ(sc->waiting_queue_size(), 1u);
}

TEST_F(SocketControlTest, TryMoveToWaitingFailsWhenQueueFull)
{
  sc->add_http_socket(50);
  sc->set_max_waiting_connections(1);

  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));

  int base = sc->get_dyn_socket_base();

  // First should succeed
  EXPECT_TRUE(sc->try_move_to_waiting(base));
  EXPECT_EQ(sc->waiting_queue_size(), 1u);

  // Second should fail (queue full)
  // After the first move, fd=101 is now at base
  EXPECT_FALSE(sc->try_move_to_waiting(base));

  // fd=101 should still be in the poll set (not removed)
  EXPECT_EQ(sc->get_sockets_size(), base + 1);
}

TEST_F(SocketControlTest, TryMoveToWaitingUnlimitedWhenZero)
{
  sc->add_http_socket(50);
  sc->set_max_waiting_connections(0);// unlimited

  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));
  sc->add_dyn_socket(make_dyn_socket(102));

  int base = sc->get_dyn_socket_base();

  EXPECT_TRUE(sc->try_move_to_waiting(base));
  EXPECT_TRUE(sc->try_move_to_waiting(base));
  EXPECT_TRUE(sc->try_move_to_waiting(base));
  EXPECT_EQ(sc->waiting_queue_size(), 3u);
}

// --- get_waiting ---

TEST_F(SocketControlTest, GetWaitingFromEmptyQueueReturnsFalse)
{
  shttps::SocketControl::SocketInfo waiting;
  EXPECT_FALSE(sc->get_waiting(waiting, noop_close));
}

TEST_F(SocketControlTest, GetWaitingReturnsQueuedSocket)
{
  auto [fd0, fd0_peer] = make_socketpair();
  ASSERT_NE(fd0, -1);

  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(fd0));

  EXPECT_TRUE(sc->try_move_to_waiting(sc->get_dyn_socket_base()));

  shttps::SocketControl::SocketInfo waiting;
  EXPECT_TRUE(sc->get_waiting(waiting, noop_close));
  EXPECT_EQ(waiting.sid, fd0);

  // Queue should now be empty
  EXPECT_FALSE(sc->get_waiting(waiting, noop_close));

  close(fd0);
  close(fd0_peer);
}

// --- get_waiting liveness check ---

TEST_F(SocketControlTest, GetWaitingDiscardsDeadSockets)
{
  auto [fd_dead, fd_dead_peer] = make_socketpair();
  auto [fd_alive, fd_alive_peer] = make_socketpair();
  ASSERT_NE(fd_dead, -1);
  ASSERT_NE(fd_alive, -1);

  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(fd_dead));
  sc->add_dyn_socket(make_dyn_socket(fd_alive));

  int base = sc->get_dyn_socket_base();
  EXPECT_TRUE(sc->try_move_to_waiting(base));// fd_dead
  EXPECT_TRUE(sc->try_move_to_waiting(base));// fd_alive

  // Close the dead socket's peer — makes the fd show POLLHUP
  close(fd_dead_peer);

  // get_waiting should skip the dead socket and return the live one
  shttps::SocketControl::SocketInfo waiting;
  EXPECT_TRUE(sc->get_waiting(waiting, noop_close));
  EXPECT_EQ(waiting.sid, fd_alive);

  close(fd_alive);
  close(fd_alive_peer);
}

TEST_F(SocketControlTest, GetWaitingReturnsFalseWhenAllDead)
{
  auto [fd0, fd0_peer] = make_socketpair();
  ASSERT_NE(fd0, -1);

  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(fd0));

  EXPECT_TRUE(sc->try_move_to_waiting(sc->get_dyn_socket_base()));

  // Close the peer — makes fd0 dead
  close(fd0_peer);

  shttps::SocketControl::SocketInfo waiting;
  EXPECT_FALSE(sc->get_waiting(waiting, noop_close));
  EXPECT_EQ(sc->waiting_queue_size(), 0u);
}

// --- close_all_dynsocks ---

TEST_F(SocketControlTest, CloseAllDynsocksClosesAllSockets)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));
  sc->add_dyn_socket(make_dyn_socket(102));
  sc->add_dyn_socket(make_dyn_socket(103));

  // Count sockets before
  int dyn_count_before = sc->get_sockets_size() - sc->get_dyn_socket_base();
  EXPECT_EQ(dyn_count_before, 4);

  sc->close_all_dynsocks(noop_close);

  // Fixed: collect-then-clear pattern closes all sockets correctly
  int dyn_count_after = sc->get_sockets_size() - sc->get_dyn_socket_base();
  EXPECT_EQ(dyn_count_after, 0);
}

TEST_F(SocketControlTest, CloseAllDynsocksNoDynSocketsIsNoop)
{
  sc->add_http_socket(50);
  sc->close_all_dynsocks(noop_close);
  EXPECT_EQ(sc->get_sockets_size(), kNThreads + 1);// control sockets + HTTP
}

// --- Reverse iteration safety: multiple mutations in reverse index order ---

TEST_F(SocketControlTest, ReverseRemovalDoesNotCorruptIndices)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));
  sc->add_dyn_socket(make_dyn_socket(102));
  sc->add_dyn_socket(make_dyn_socket(103));

  int base = sc->get_dyn_socket_base();

  // Simulate reverse iteration: remove from highest index to lowest
  shttps::SocketControl::SocketInfo removed;

  sc->remove(base + 3, removed);
  EXPECT_EQ(removed.sid, 103);

  sc->remove(base + 1, removed);
  EXPECT_EQ(removed.sid, 101);

  // Remaining: fd=100 at base, fd=102 at base+1
  pollfd *arr = sc->get_sockets_arr();
  EXPECT_EQ(arr[base].fd, 100);
  EXPECT_EQ(arr[base + 1].fd, 102);
  EXPECT_EQ(sc->get_sockets_size(), base + 2);
}

TEST_F(SocketControlTest, ReverseMoveToWaitingDoesNotCorruptIndices)
{
  auto [fd0, fd0_peer] = make_socketpair();
  auto [fd2, fd2_peer] = make_socketpair();
  ASSERT_NE(fd0, -1);
  ASSERT_NE(fd2, -1);

  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(fd0));
  sc->add_dyn_socket(make_dyn_socket(101));
  sc->add_dyn_socket(make_dyn_socket(fd2));

  int base = sc->get_dyn_socket_base();

  // Reverse order: move fd2 (base+2) then fd0 (base)
  EXPECT_TRUE(sc->try_move_to_waiting(base + 2));
  EXPECT_TRUE(sc->try_move_to_waiting(base));

  // Only fd=101 should remain in poll set
  pollfd *arr = sc->get_sockets_arr();
  EXPECT_EQ(arr[base].fd, 101);
  EXPECT_EQ(sc->get_sockets_size(), base + 1);

  // Waiting queue should have fd2, fd0 in FIFO order
  shttps::SocketControl::SocketInfo w;
  EXPECT_TRUE(sc->get_waiting(w, noop_close));
  EXPECT_EQ(w.sid, fd2);
  EXPECT_TRUE(sc->get_waiting(w, noop_close));
  EXPECT_EQ(w.sid, fd0);

  close(fd0); close(fd0_peer);
  close(fd2); close(fd2_peer);
}

TEST_F(SocketControlTest, AddDynSocketDuringReverseIterationIsNotVisited)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));

  int size_before_add = sc->get_sockets_size();

  sc->add_dyn_socket(make_dyn_socket(200));

  EXPECT_EQ(sc->get_sockets_size(), size_before_add + 1);

  pollfd *arr = sc->get_sockets_arr();
  EXPECT_EQ(arr[sc->get_sockets_size() - 1].fd, 200);
}

// --- Forward removal demonstrates the bug (for documentation) ---

TEST_F(SocketControlTest, ForwardRemovalSkipsSockets_DocumentsBug)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));
  sc->add_dyn_socket(make_dyn_socket(102));
  sc->add_dyn_socket(make_dyn_socket(103));

  int base = sc->get_dyn_socket_base();

  shttps::SocketControl::SocketInfo removed;

  sc->remove(base, removed);
  EXPECT_EQ(removed.sid, 100);

  sc->remove(base + 1, removed);
  EXPECT_EQ(removed.sid, 102);// 101 was skipped — this is the bug

  pollfd *arr = sc->get_sockets_arr();
  EXPECT_EQ(arr[base].fd, 101);
  EXPECT_EQ(arr[base + 1].fd, 103);
}

// --- collect_expired_waiting ---

TEST_F(SocketControlTest, CollectExpiredWaitingRemovesStaleEntries)
{
  sc->add_http_socket(50);
  sc->set_queue_timeout(0);// 0 seconds = everything expires immediately

  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));

  int base = sc->get_dyn_socket_base();
  EXPECT_TRUE(sc->try_move_to_waiting(base));
  EXPECT_TRUE(sc->try_move_to_waiting(base));

  EXPECT_EQ(sc->waiting_queue_size(), 2u);

  auto expired = sc->collect_expired_waiting();
  EXPECT_EQ(expired.size(), 2u);
  EXPECT_EQ(sc->waiting_queue_size(), 0u);
}

TEST_F(SocketControlTest, CollectExpiredWaitingKeepsFreshEntries)
{
  sc->add_http_socket(50);
  sc->set_queue_timeout(60);// 60 seconds — nothing should expire

  sc->add_dyn_socket(make_dyn_socket(100));

  int base = sc->get_dyn_socket_base();
  EXPECT_TRUE(sc->try_move_to_waiting(base));

  auto expired = sc->collect_expired_waiting();
  EXPECT_EQ(expired.size(), 0u);
  EXPECT_EQ(sc->waiting_queue_size(), 1u);
}

// --- collect_all_waiting ---

TEST_F(SocketControlTest, CollectAllWaitingReturnsAllQueuedSockets)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));
  sc->add_dyn_socket(make_dyn_socket(102));

  int base = sc->get_dyn_socket_base();
  EXPECT_TRUE(sc->try_move_to_waiting(base));
  EXPECT_TRUE(sc->try_move_to_waiting(base));
  EXPECT_TRUE(sc->try_move_to_waiting(base));

  EXPECT_EQ(sc->waiting_queue_size(), 3u);

  auto collected = sc->collect_all_waiting();
  EXPECT_EQ(collected.size(), 3u);
  EXPECT_EQ(sc->waiting_queue_size(), 0u);
}

// --- collect_idle_dynsocks (keep-alive enforcement) ---

TEST_F(SocketControlTest, CollectIdleDynsocksReapsExpiredSockets)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));

  // Sleep briefly so last_activity is in the past
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Timeout of 0 seconds — everything should be reaped
  auto reaped = sc->collect_idle_dynsocks(0);
  EXPECT_EQ(reaped.size(), 2u);

  // Only HTTP socket + control sockets should remain
  int base = sc->get_dyn_socket_base();
  EXPECT_EQ(sc->get_sockets_size(), base);
}

TEST_F(SocketControlTest, CollectIdleDynsocksKeepsFreshSockets)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));

  // Timeout of 60 seconds — nothing should be reaped
  auto reaped = sc->collect_idle_dynsocks(60);
  EXPECT_EQ(reaped.size(), 0u);
  EXPECT_EQ(sc->get_sockets_size(), sc->get_dyn_socket_base() + 1);
}

TEST_F(SocketControlTest, CollectIdleDynsocksNoDynSockets)
{
  sc->add_http_socket(50);
  auto reaped = sc->collect_idle_dynsocks(0);
  EXPECT_EQ(reaped.size(), 0u);
}

// --- enqueue_time is set ---

TEST_F(SocketControlTest, TryMoveToWaitingSetsEnqueueTime)
{
  auto [fd0, fd0_peer] = make_socketpair();
  ASSERT_NE(fd0, -1);

  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(fd0));

  auto before = std::chrono::steady_clock::now();
  EXPECT_TRUE(sc->try_move_to_waiting(sc->get_dyn_socket_base()));

  shttps::SocketControl::SocketInfo waiting;
  EXPECT_TRUE(sc->get_waiting(waiting, noop_close));
  EXPECT_GE(waiting.enqueue_time, before);
  EXPECT_LE(waiting.enqueue_time, std::chrono::steady_clock::now());

  close(fd0);
  close(fd0_peer);
}

// --- last_activity is set ---

TEST_F(SocketControlTest, AddDynSocketSetsLastActivity)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));

  // Verify by sweeping with a very long timeout — nothing should be reaped
  auto reaped = sc->collect_idle_dynsocks(3600);
  EXPECT_EQ(reaped.size(), 0u);
}

// --- waiting_queue_size ---

TEST_F(SocketControlTest, WaitingQueueSizeTracksCorrectly)
{
  auto [fd0, fd0_peer] = make_socketpair();
  auto [fd1, fd1_peer] = make_socketpair();
  ASSERT_NE(fd0, -1);
  ASSERT_NE(fd1, -1);

  sc->add_http_socket(50);
  EXPECT_EQ(sc->waiting_queue_size(), 0u);

  sc->add_dyn_socket(make_dyn_socket(fd0));
  sc->add_dyn_socket(make_dyn_socket(fd1));

  int base = sc->get_dyn_socket_base();
  EXPECT_TRUE(sc->try_move_to_waiting(base));
  EXPECT_EQ(sc->waiting_queue_size(), 1u);

  EXPECT_TRUE(sc->try_move_to_waiting(base));
  EXPECT_EQ(sc->waiting_queue_size(), 2u);

  shttps::SocketControl::SocketInfo w;
  sc->get_waiting(w, noop_close);
  EXPECT_EQ(sc->waiting_queue_size(), 1u);

  close(fd0); close(fd0_peer);
  close(fd1); close(fd1_peer);
}

}// namespace
