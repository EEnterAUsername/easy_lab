#include "uthread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct uthread *current_thread = NULL;
static struct uthread *main_thread = NULL;

#define MAX_LEN_Q 114

struct uthread_queue {
    struct uthread *queue[MAX_LEN_Q];
    int front, rear;
};

static struct uthread_queue Q_init;

void init_(struct uthread_queue *q_) {
    q_->front = -1;
    q_->rear = -1;
}

void insert_(struct uthread_queue *q_, struct uthread *thread) {
    if (q_->rear == MAX_LEN_Q - 1) {
        printf("@@@ full !!!!\n");
        exit(-1);
    }
    q_->rear++;
    q_->queue[q_->rear] = thread;
    if (q_->front == -1) {
        q_->front = 0;
    }
}

struct uthread *pop_(struct uthread_queue *q_) {
    if (q_->front == -1) {
        printf("@@@ empty !!!!\n");
        exit(-1);
    }
    struct uthread *thread = q_->queue[q_->front];
    for (int i = q_->front; i < q_->rear; i++) {
        q_->queue[i] = q_->queue[i + 1];
    }
    if (q_->front == q_->rear) {
        q_->front = q_->rear = -1;
    } else {
        q_->rear--;
    }
    return thread;
}


/// @brief 切换上下文
/// @param from 当前上下文
/// @param to 要切换到的上下文
extern void thread_switch(struct context *from, struct context *to);

/// @brief 线程的入口函数
/// @param tcb 线程的控制块
/// @param thread_func 线程的执行函数
/// @param arg 线程的参数
void _uthread_entry(struct uthread *tcb, void (*thread_func)(void *),
                    void *arg);

/// @brief 清空上下文结构体
/// @param context 上下文结构体指针
static inline void make_dummpy_context(struct context *context) {
  memset((struct context *)context, 0, sizeof(struct context));
}

struct uthread *uthread_create(void (*func)(void *), void *arg,const char* thread_name) {
  struct uthread *uthread = NULL;
  int ret;

  // 申请一块16字节对齐的内存
  ret = posix_memalign((void **)&uthread, 16, sizeof(struct uthread));
  if (0 != ret) {
    printf("error");
    exit(-1);
  }

  //         +------------------------+
  // low     |                        |
  //         |                        |
  //         |                        |
  //         |         stack          |
  //         |                        |
  //         |                        |
  //         |                        |
  //         +------------------------+
  //  high   |    fake return addr    |
  //         +------------------------+

  // 初始化uthread结构体，包括设置rip, rsp等寄存器，入口地址为函数_uthread_entry
  long long sp = ((long long)&uthread->stack + STACK_SIZE) & (~(long long)15);
  sp -= 8; // 对齐
  uthread->context.rsp = sp;
  uthread->context.rip = (long long)_uthread_entry;
  uthread->context.rdi = (long long)uthread;
  uthread->context.rsi = (long long)func;
  uthread->context.rdx = (long long)arg;
  uthread->name = thread_name;
  uthread->state = THREAD_INIT;
  // 插入新创建的线程到队列
  insert_(&Q_init, uthread);
  // 在队列中插入新创建的线程
  // 插入到队列中，可能是Q_init或其他队列
  // insert_(&Q_init, uthread);

  return uthread;
}

void schedule() {

  // 实现一个FIFO队列
  if (Q_init.front == -1) { // 如果队列为空，说明没有待调度的线程
    struct uthread *pre_thread = current_thread;
    current_thread = main_thread;
    thread_switch(&pre_thread->context, &current_thread->context); // 切换到主线程
    current_thread->state = THREAD_STOP; // 设置主线程为停止态
    thread_destory(current_thread); // 销毁主线程
  }
  // 取出下个线程
  struct uthread *next_thread = pop_(&Q_init);
  if (next_thread->state == THREAD_INIT) { // 若是初始态
    struct uthread *previous_thread = current_thread;
    current_thread = next_thread;
    thread_switch(&previous_thread->context, &current_thread->context);
  } 
  else if (next_thread->state == THREAD_SUSPENDED) {// 若是挂起态
    uthread_resume(next_thread);// 恢复
  } 
  else if (next_thread->state == THREAD_STOP) {// 若是停止态
    printf("!!! stop !!!\n");
  }
}

long long uthread_yield() {
    current_thread->state = THREAD_SUSPENDED;
    insert_(&Q_init, current_thread); // 插入当前线程到队列
    schedule(); // 调度下一个线程执行
    return 0;
}


void uthread_resume(struct uthread *tcb) {
  /*
  TODO：调度器恢复到一个函数的上下文。
  */
  struct uthread *pre_thread = current_thread;
  current_thread = tcb;
  // 恢复线程的执行，将其状态设置为运行
  tcb->state = THREAD_RUNNING;
  thread_switch(&pre_thread->context, &tcb->context);
}

void thread_destory(struct uthread *tcb) {
  free(tcb);
}

void _uthread_entry(struct uthread *tcb, void (*thread_func)(void *),
                    void *arg) {
  /*
  这是所有用户态线程函数开始执行的入口。在这个函数中，你需要传参数给真正调用的函数，然后设置tcb的状态。
  */
  tcb->state = THREAD_RUNNING; // 设置线程状态为运行
  // 调用线程函数，传递参数
  thread_func(arg);
  tcb->state = THREAD_STOP; // 设置线程状态为停止
  free(tcb); // 释放线程控制块内存
  schedule(); // 调度下一个线程执行
}

void init_uthreads() {
    main_thread = malloc(sizeof(struct uthread));
    make_dummpy_context(&main_thread->context);
 
    // 初始化其他数据结构和变量
    init_(&Q_init);

    long long sp = ((long long)&main_thread->stack + STACK_SIZE) & (~(long long)15);
    sp -= 8; // 对齐
    main_thread->context.rsp = sp;
    main_thread->context.rip = (long long)_uthread_entry;
    main_thread->context.rdi = (long long)main_thread;
    main_thread->context.rsi = (long long)NULL;
    main_thread->context.rdx = (long long)NULL;
    main_thread->state = THREAD_RUNNING;
    main_thread->name = "main_thread";

    current_thread = main_thread;
}
