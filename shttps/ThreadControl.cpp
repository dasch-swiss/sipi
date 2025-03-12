/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ThreadControl.h"
#include <cstring>
#include <iostream>
#include <syslog.h>

#include <source_location>

#include "Logger.h"

namespace shttps {

ThreadControl::ThreadControl(int n_threads, void *(*start_routine)(void *), Server *serv)
{
  // thread_list.reserve(n_threads);
  // child_data.reserve(n_threads);
  //
  //  first we have to create the vector for the child data. We misuse the "result"-field
  //  to store the master's socket endpoint.
  //
  for (int n = 0; n < n_threads; n++) {
    int control_pipe[2];
    if (socketpair(PF_LOCAL, SOCK_STREAM, 0, control_pipe) != 0) {
      const auto loc = std::source_location::current();
      log_err("Creating pipe failed at [%s: %d]: %m", loc.file_name(), loc.line());
      return;
    }
    child_data.push_back({ control_pipe[1], control_pipe[0], serv });
  }
  //
  // now we can create the threads
  //
  ThreadChildData *cd = child_data.data();// we get the raw array of child data
  for (int n = 0; n < n_threads; n++) {
    ThreadMasterData thread_data;
    thread_data.control_pipe = child_data[n].result;
    pthread_attr_t tattr;
    pthread_attr_init(&tattr);
    if (pthread_create(&thread_data.tid, &tattr, start_routine, (void *)(&cd[n])) < 0) {
      const auto loc = std::source_location::current();
      log_err("Could not create thread at [%s: %d]: %m", loc.file_name(), loc.line());
      break;
    }
    thread_list.push_back(thread_data);
    thread_push(thread_data);
  }
}
//=========================================================================

ThreadControl::~ThreadControl()
{
  for (auto const &thread : thread_list) {
    int err = pthread_join(thread.tid, nullptr);
    if (err != 0) { log_info("pthread_join failed with error code: %s", strerror(err)); }
  }
}
//=========================================================================

void ThreadControl::thread_push(const ThreadMasterData &tinfo)
{
  std::unique_lock<std::mutex> thread_queue_guard(thread_queue_mutex);
  thread_queue.push(tinfo);
}
//=========================================================================

bool ThreadControl::thread_pop(ThreadMasterData &tinfo)
{
  std::unique_lock<std::mutex> thread_queue_guard(thread_queue_mutex);
  tinfo = { 0, 0 };
  if (!thread_queue.empty()) {
    tinfo = thread_queue.front();
    thread_queue.pop();
    return true;
  }
  return false;
}
//=========================================================================

ThreadControl::ThreadMasterData &ThreadControl::operator[](size_t index)
{
  if (index < thread_list.size()) {
    return thread_list[index];
  } else {
    throw ThreadControlErrors::INVALID_INDEX;
  }
}
//=========================================================================

int ThreadControl::thread_delete(int pos)
{
  thread_list.erase(thread_list.begin() + pos);
  return thread_list.size();
}
//=========================================================================

}// namespace shttps
