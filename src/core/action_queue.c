/**
 * @file action_queue.c
 * @brief Implementation of asynchronous non-blocking action execution queue.
 */

#include "action_queue.h"
#include "inject.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_QUEUE_DEPTH 64

typedef struct action_node {
    openlogi_action_t action;
    struct action_node *next;
} action_node_t;

static pthread_t g_worker_thread;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_idle_cond = PTHREAD_COND_INITIALIZER;

static action_node_t *g_head = NULL;
static action_node_t *g_tail = NULL;
static size_t g_queue_len = 0;
static bool g_running = false;
static bool g_busy = false;

static void free_node(action_node_t *node)
{
    if (!node) return;
    if (node->action.type == ACTION_WORKFLOW && node->action.u.workflow) {
        free(node->action.u.workflow);
        node->action.u.workflow = NULL;
    }
    free(node);
}

static void *action_worker_main(void *arg)
{
    (void)arg;

    while (1) {
        pthread_mutex_lock(&g_lock);
        while (g_running && g_head == NULL) {
            g_busy = false;
            pthread_cond_broadcast(&g_idle_cond);
            pthread_cond_wait(&g_cond, &g_lock);
        }

        if (!g_running && g_head == NULL) {
            g_busy = false;
            pthread_cond_broadcast(&g_idle_cond);
            pthread_mutex_unlock(&g_lock);
            break;
        }

        action_node_t *node = g_head;
        g_head = node->next;
        if (!g_head) {
            g_tail = NULL;
        }
        g_queue_len--;
        g_busy = true;
        pthread_mutex_unlock(&g_lock);

        /* Execute action outside of queue lock to avoid stalling the queue */
        inject_action(&node->action);

        free_node(node);
    }

    return NULL;
}

bool action_queue_init(void)
{
    pthread_mutex_lock(&g_lock);
    if (g_running) {
        pthread_mutex_unlock(&g_lock);
        return true;
    }

    g_running = true;
    g_busy = false;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
    int thread_rc = pthread_create(&g_worker_thread, &attr, action_worker_main, NULL);
    pthread_attr_destroy(&attr);

    if (thread_rc != 0) {
        g_running = false;
        pthread_mutex_unlock(&g_lock);
        return false;
    }

    pthread_mutex_unlock(&g_lock);
    return true;
}

bool action_queue_is_blocking(const openlogi_action_t *action)
{
    if (!action) return false;
    switch (action->type) {
        case ACTION_WORKFLOW:
        case ACTION_SHELL_COMMAND:
        case ACTION_APP_OPEN:
        case ACTION_APPLE_SCRIPT:
        case ACTION_SLEEP:
            return true;
        default:
            return false;
    }
}

bool action_queue_enqueue(const openlogi_action_t *action)
{
    if (!action) return false;

    pthread_mutex_lock(&g_lock);
    if (!g_running || g_queue_len >= MAX_QUEUE_DEPTH) {
        pthread_mutex_unlock(&g_lock);
        return false;
    }

    action_node_t *node = (action_node_t *)calloc(1, sizeof(action_node_t));
    if (!node) {
        pthread_mutex_unlock(&g_lock);
        return false;
    }

    node->action = *action;
    if (action->type == ACTION_WORKFLOW && action->u.workflow != NULL) {
        node->action.u.workflow = (openlogi_workflow_t *)calloc(1, sizeof(openlogi_workflow_t));
        if (!node->action.u.workflow) {
            free(node);
            pthread_mutex_unlock(&g_lock);
            return false;
        }
        *node->action.u.workflow = *action->u.workflow;
    }

    node->next = NULL;
    if (g_tail) {
        g_tail->next = node;
        g_tail = node;
    } else {
        g_head = node;
        g_tail = node;
    }
    g_queue_len++;

    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_lock);
    return true;
}

size_t action_queue_pending_count(void)
{
    pthread_mutex_lock(&g_lock);
    size_t count = g_queue_len;
    pthread_mutex_unlock(&g_lock);
    return count;
}

void action_queue_wait_idle(void)
{
    pthread_mutex_lock(&g_lock);
    while (g_queue_len > 0 || g_busy) {
        pthread_cond_wait(&g_idle_cond, &g_lock);
    }
    pthread_mutex_unlock(&g_lock);
}

void action_queue_clear_pending(void)
{
    pthread_mutex_lock(&g_lock);
    action_node_t *cur = g_head;
    while (cur) {
        action_node_t *next = cur->next;
        free_node(cur);
        cur = next;
    }
    g_head = NULL;
    g_tail = NULL;
    g_queue_len = 0;
    pthread_mutex_unlock(&g_lock);
}

void action_queue_shutdown(void)
{
    pthread_mutex_lock(&g_lock);
    if (!g_running) {
        pthread_mutex_unlock(&g_lock);
        return;
    }

    g_running = false;
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_lock);

    pthread_join(g_worker_thread, NULL);

    pthread_mutex_lock(&g_lock);
    action_node_t *cur = g_head;
    while (cur) {
        action_node_t *next = cur->next;
        free_node(cur);
        cur = next;
    }
    g_head = NULL;
    g_tail = NULL;
    g_queue_len = 0;
    g_busy = false;
    pthread_mutex_unlock(&g_lock);
}
