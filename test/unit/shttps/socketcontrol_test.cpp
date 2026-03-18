/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gtest/gtest.h"

#include "shttps/SocketControl.h"
#include "shttps/ThreadControl.h"

namespace {

// No-op thread function — exits immediately, leaving socketpair fds valid for SocketControl
void *noop_thread(void *) { return nullptr; }

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

  // Create a fake DYN_SOCKET SocketInfo with a given fd number
  static shttps::SocketControl::SocketInfo make_dyn_socket(int fd)
  {
    return shttps::SocketControl::SocketInfo(
      shttps::SocketControl::NOOP, shttps::SocketControl::DYN_SOCKET, fd);
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

// --- move_to_waiting ---

TEST_F(SocketControlTest, MoveToWaitingRemovesFromPollSetAndQueues)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));

  int size_before = sc->get_sockets_size();
  int base = sc->get_dyn_socket_base();

  sc->move_to_waiting(base);// move socket fd=100

  EXPECT_EQ(sc->get_sockets_size(), size_before - 1);

  // The moved socket should be retrievable from waiting queue
  shttps::SocketControl::SocketInfo waiting;
  EXPECT_TRUE(sc->get_waiting(waiting));
  EXPECT_EQ(waiting.sid, 100);
}

TEST_F(SocketControlTest, MoveToWaitingMultipleSocketsPreservesFIFO)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));
  sc->add_dyn_socket(make_dyn_socket(102));

  int base = sc->get_dyn_socket_base();

  // Move 100 first, then 101 (which is now at base after 100 was removed)
  sc->move_to_waiting(base);// fd=100
  sc->move_to_waiting(base);// fd=101 (shifted to base after 100 was removed)

  // FIFO: 100 should come out first, then 101
  shttps::SocketControl::SocketInfo w1, w2;
  EXPECT_TRUE(sc->get_waiting(w1));
  EXPECT_EQ(w1.sid, 100);
  EXPECT_TRUE(sc->get_waiting(w2));
  EXPECT_EQ(w2.sid, 101);
}

// --- get_waiting ---

TEST_F(SocketControlTest, GetWaitingFromEmptyQueueReturnsFalse)
{
  shttps::SocketControl::SocketInfo waiting;
  EXPECT_FALSE(sc->get_waiting(waiting));
}

TEST_F(SocketControlTest, GetWaitingReturnsQueuedSocket)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));

  sc->move_to_waiting(sc->get_dyn_socket_base());

  shttps::SocketControl::SocketInfo waiting;
  EXPECT_TRUE(sc->get_waiting(waiting));
  EXPECT_EQ(waiting.sid, 100);

  // Queue should now be empty
  EXPECT_FALSE(sc->get_waiting(waiting));
}

// --- close_all_dynsocks (documents the iterator bug) ---

TEST_F(SocketControlTest, CloseAllDynsocksClosesAllSockets)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));
  sc->add_dyn_socket(make_dyn_socket(102));
  sc->add_dyn_socket(make_dyn_socket(103));

  std::vector<int> closed_fds;
  auto close_func = [](const shttps::SocketControl::SocketInfo &si) -> int {
    // Can't capture in a C function pointer — use a static vector
    return 0;
  };

  // Count sockets before
  int dyn_count_before = sc->get_sockets_size() - sc->get_dyn_socket_base();
  EXPECT_EQ(dyn_count_before, 4);

  sc->close_all_dynsocks(close_func);

  // BUG: The current implementation skips every other socket due to the forward-erase
  // iterator bug. After the fix, all 4 sockets should be closed and the vector should
  // have no dyn sockets remaining.
  //
  // Current buggy behavior: only 2 of 4 sockets are closed.
  // After fix: this test should verify get_sockets_size() == dyn_socket_base
  //
  // For now, we document the bug:
  int dyn_count_after = sc->get_sockets_size() - sc->get_dyn_socket_base();
  // TODO(DEV-6024): After fixing close_all_dynsocks, change this to:
  // EXPECT_EQ(dyn_count_after, 0);
  EXPECT_EQ(dyn_count_after, 2);// BUG: skips every other socket
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
  // This is the pattern used by the fix in Phase 1
  shttps::SocketControl::SocketInfo removed;

  // Remove fd=103 (index base+3)
  sc->remove(base + 3, removed);
  EXPECT_EQ(removed.sid, 103);

  // Remove fd=101 (index base+1) — still valid after removing base+3
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
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));
  sc->add_dyn_socket(make_dyn_socket(102));

  int base = sc->get_dyn_socket_base();

  // Reverse order: move 102 (base+2) then 100 (base)
  sc->move_to_waiting(base + 2);// fd=102
  sc->move_to_waiting(base);// fd=100

  // Only fd=101 should remain in poll set
  pollfd *arr = sc->get_sockets_arr();
  EXPECT_EQ(arr[base].fd, 101);
  EXPECT_EQ(sc->get_sockets_size(), base + 1);

  // Waiting queue should have 102, 100 in FIFO order
  shttps::SocketControl::SocketInfo w;
  EXPECT_TRUE(sc->get_waiting(w));
  EXPECT_EQ(w.sid, 102);
  EXPECT_TRUE(sc->get_waiting(w));
  EXPECT_EQ(w.sid, 100);
}

TEST_F(SocketControlTest, AddDynSocketDuringReverseIterationIsNotVisited)
{
  sc->add_http_socket(50);
  sc->add_dyn_socket(make_dyn_socket(100));
  sc->add_dyn_socket(make_dyn_socket(101));

  int size_before_add = sc->get_sockets_size();

  // Simulate: during reverse iteration, a FINISHED_AND_CONTINUE adds a keep-alive socket
  sc->add_dyn_socket(make_dyn_socket(200));

  // The appended socket is at the end — in reverse iteration starting from
  // size_before_add - 1, we would NOT visit the new socket at index size_before_add
  // (since we already passed it). This is correct behavior.
  EXPECT_EQ(sc->get_sockets_size(), size_before_add + 1);

  // The new socket should be visible on the NEXT get_sockets_arr() call
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

  // Simulate forward iteration with removal (the bug):
  // i=base: remove fd=100. Now 101 is at base, 102 at base+1, 103 at base+2.
  // i=base+1: remove fd=102 (NOT 101!). 101 was skipped!
  shttps::SocketControl::SocketInfo removed;

  sc->remove(base, removed);
  EXPECT_EQ(removed.sid, 100);

  // After removal, fd=101 is now at index `base`
  // But forward iteration moves to base+1, which is now fd=102 (skipping 101!)
  sc->remove(base + 1, removed);
  EXPECT_EQ(removed.sid, 102);// 101 was skipped — this is the bug

  // fd=101 and fd=103 remain
  pollfd *arr = sc->get_sockets_arr();
  EXPECT_EQ(arr[base].fd, 101);
  EXPECT_EQ(arr[base + 1].fd, 103);
}

}// namespace
