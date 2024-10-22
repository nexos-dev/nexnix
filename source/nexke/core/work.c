/*
    work.c - implements kernel work queues
    Copyright 2024 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <assert.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <nexke/synch.h>
#include <nexke/task.h>
#include <string.h>

// Work queue cache
static SlabCache_t* nkWqCache = NULL;

// Work item cache
static SlabCache_t* nkItemCache = NULL;

// Initializes worker system
void NkInitWorkQueue()
{
    nkWqCache = MmCacheCreate (sizeof (NkWorkQueue_t), "NkWorkQueue_t", 0, 0);
    nkItemCache = MmCacheCreate (sizeof (NkWorkItem_t), "NkWorkItem_t", 0, 0);
    assert (nkWqCache && nkItemCache);
}

// Work scheduler function
static void NkWorkScheduler (void* arg)
{
    NkWorkQueue_t* queue = (NkWorkQueue_t*) arg;
    TskAcquireMutex (&queue->lock);
    while (1)
    {
        TskWaitCondition (&queue->condition, &queue->lock);
        TskUnsetCondition (&queue->condition);
        TskAcquireMutex (&queue->lock);
        // Work needs to occur now, drain queue
        while (queue->numItems)
        {
            NkLink_t* link = NkListFront (&queue->items);
            NkWorkItem_t* item = LINK_CONTAINER (link, NkWorkItem_t, link);
            queue->cb (item);    // Call item
            --queue->numItems;
            NkListRemove (&queue->items, link);
            MmCacheFree (nkItemCache, item);
        }
    }
}

// Timer handler
static void NkWorkTimer (NkTimeEvent_t* event, void* arg)
{
    NkWorkQueue_t* queue = (NkWorkQueue_t*) arg;
    TskSignalCondition (&queue->condition);
}

// Creates a new work queue
NkWorkQueue_t* NkWorkQueueCreate (NkWorkCallback cb, int type, int flags, int prio, int threshold)
{
    NkWorkQueue_t* queue = MmCacheAlloc (nkWqCache);
    if (!queue)
        NkPanicOom();
    memset (queue, 0, sizeof (NkWorkQueue_t));
    // Initialize basic fields
    queue->cb = cb;
    queue->flags = flags;
    queue->type = type;
    queue->threshold = threshold;
    NkListInit (&queue->items);
    // Setup timer stuff
    if (type == NK_WORK_TIMED)
    {
        queue->timer = NkTimeNewEvent();
        if (!queue->timer)
            NkPanicOom();
        NkTimeSetCbEvent (queue->timer, NkWorkTimer, (void*) queue);
    }
    // Create thread
    queue->thread = TskCreateThread (NkWorkScheduler, queue, "NkWorkScheduler", 0);
    if (!queue->thread)
    {
        NkTimeFreeEvent (queue->timer);
        MmCacheFree (nkWqCache, queue);
        return NULL;
    }
    // Initialize mutex and condition
    TskInitCondition (&queue->condition);
    TskInitMutex (&queue->lock);
    // Make thread ready to run
    TskStartThread (queue->thread);
    return queue;
}

// Destroys a work queue
void NkWorkQueueDestroy (NkWorkQueue_t* queue)
{
    if (TskAcquireMutex (&queue->lock) != EOK)
        return;
    // Deregister any pending timer events
    if (queue->timer)
        NkTimeDeRegEvent (queue->timer);
    // Make sure pending waiters know we are destroyed
    TskCloseCondition (&queue->condition);
    TskCloseMutex (&queue->lock);
    // Now destroy everything
    NkTimeFreeEvent (queue->timer);
    // TskTerminateThread(&queue->thread,0);
    MmCacheFree (nkWqCache, queue);
}

// Arms timer on a timed work queue
bool NkWorkQueueArmTimer (NkWorkQueue_t* queue, ktime_t delta)
{
    if (queue->type != NK_WORK_TIMED)
        return false;
    if (TskAcquireMutex (&queue->lock) != EOK)
        return false;
    // Register a timer for this
    if (queue->flags & NK_WORK_ONESHOT)
        NkTimeRegEvent (queue->timer, delta, 0);
    else
        NkTimeRegEvent (queue->timer, delta, NK_TIME_REG_PERIODIC);
    TskReleaseMutex (&queue->lock);
}

// Submits work to queue
NkWorkItem_t* NkWorkQueueSubmit (NkWorkQueue_t* queue, void* data)
{
    if (TskAcquireMutex (&queue->lock) != EOK)
        return NULL;
    NkWorkItem_t* item = MmCacheAlloc (nkItemCache);
    item->queue = queue;
    item->data = data;
    // Enqueue the work
    NkListAddBack (&queue->items, &item->link);
    ++queue->numItems;
    // Determine if we need to do work now
    if (queue->numItems >= queue->threshold && queue->type == NK_WORK_DEMAND)
        TskBroadcastCondition (&queue->condition);
    TskReleaseMutex (&queue->lock);
    return item;
}

// Removes work from queue
bool NkWorkQueueCancel (NkWorkQueue_t* queue, NkWorkItem_t* item)
{
    if (TskAcquireMutex (&queue->lock) != EOK)
        return NULL;
    // Remove item from queue
    NkListRemove (&queue->items, &item->link);
    --queue->numItems;
    // Free it
    MmCacheFree (nkItemCache, item);
    TskReleaseMutex (&queue->lock);
    return true;
}
