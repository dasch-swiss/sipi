/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <pthread.h>//for threading , link with lpthread
#include <sys/socket.h>
#include <sys/types.h>
// #include <thread>
#include <mutex>
#include <poll.h>
#include <queue>
#include <syslog.h>
#include <unistd.h>
#include <vector>

#include <atomic>

#ifndef SIPI_THREADCONTROL_H
#define SIPI_THREADCONTROL_H

namespace shttps {

class Server;// Declaration only

class ThreadControl
{
public:
  typedef struct
  {
    pthread_t tid;
    int control_pipe;
  } ThreadMasterData;

  typedef struct
  {
    int control_pipe;
    int result;
    Server *serv;
  } ThreadChildData;

  typedef enum { INVALID_INDEX } ThreadControlErrors;

private:
  std::vector<ThreadMasterData> thread_list;//!> List of all threads
  std::vector<ThreadChildData> child_data;//!> Data given to the thread
  std::queue<ThreadMasterData> thread_queue;//!> Queue of available threads for processing
  std::mutex thread_queue_mutex;

public:
  ThreadControl(int n_threads, void *(*start_routine)(void *), Server *serv);

  ~ThreadControl();

  void thread_push(const ThreadMasterData &tinfo);

  bool thread_pop(ThreadMasterData &tinfo);

  int thread_delete(int pos);

  ThreadMasterData &operator[](int index);

  inline int nthreads() const { return thread_list.size(); }

  void join_all();
};

}// namespace shttps

#endif// SIPI_THREADCONTROL_H
