/*
    time.c - contains system timer event manager
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
#include <string.h>

// Platform pointer
static NkPlatform_t* platform = NULL;

// Event cache
static SlabCache_t* nkEventCache = NULL;

// Allocates a timer event
NkTimeEvent_t* NkTimeNewEvent()
{
    return MmCacheAlloc (nkEventCache);
}

// Frees a timer event
void NkTimeFreeEvent (NkTimeEvent_t* event)
{
    assert (!event->inUse);
    MmCacheFree (nkEventCache, event);
}

// Converts a delta to a deadline
static ktime_t NkTimeDeltaToDeadline (ktime_t* delta)
{
    // Get ref tick
    ktime_t refTick = platform->clock->getTime();
    ktime_t deadline = refTick + *delta;
    // If deadline equals ref tick (i.e., delta is 0) increase it by one tick
    if (refTick == deadline)
    {
        ++*delta;
        ++deadline;
    }
    return deadline;
}

// Admits event into queue
static FORCEINLINE void nkTimeEvtAdmit (NkCcb_t* ccb, NkTimeEvent_t* event, ktime_t delta)
{
    // Grab list
    NkList_t* list = &ccb->timeEvents;
    NkLink_t* iter = NkListFront (list);
    // Find where in list to add
    // Basically the list is sorted by when event deadline is. Earliest deadline is first
    if (!iter)
    {
        // List is empty, add to front
        NkListAddFront (list, &event->link);
    }
    else
    {
        // Loop through list to find spot
        while (iter)
        {
            NkTimeEvent_t* cur = LINK_CONTAINER (iter, NkTimeEvent_t, link);
            // If it meets the deadline or if this is the last entry, add here
            if (event->deadline < cur->deadline)
            {
                // Found spot, add it here
                NkListAddBefore (list, iter, &event->link);
                break;    // Exit loop
            }
            else if (!iter->next)
            {
                // Add after
                NkListAdd (list, iter, &event->link);
                break;
            }
            iter = NkListIterate (iter);    // To next spot
        }
    }
    // If this event is in the front, then we need to arm the timer
    // NOTE: this is only true if we are not using a software timer.
    // In that case, we have no need to arm anything
    if (&event->link == NkListFront (&ccb->timeEvents))
    {
        // Arm timer if needed
        if (platform->timer->type != PLT_TIMER_SOFT)
        {
            // Arm timer
            platform->timer->armTimer (delta);
            ccb->nextDeadline = event->deadline;
        }
    }
}

// Removes event from queue
static FORCEINLINE void nkTimeEvtRemove (NkCcb_t* ccb, NkTimeEvent_t* event)
{
    NkList_t* list = &ccb->timeEvents;
    // Figure out if this is the head
    bool isHead = false;
    if (&event->link == NkListFront (list))
        isHead = true;
    // Remove this event from list
    NkListRemove (list, &event->link);
    event->inUse = false;
    // Set head if needed
    if (isHead)
    {
        // Now we need to re-arm the timer
        if (platform->timer->type != PLT_TIMER_SOFT)
        {
            ktime_t deadline = event->deadline;
            int64_t delta = deadline - platform->clock->getTime();
            if (delta < 0)
            {
                // TODO: is there a better way of handling this?
                delta = 0;
            }
            platform->timer->armTimer (delta);
            ccb->nextDeadline = event->deadline;
        }
    }
    event->link.next = event->link.prev = NULL;
}

// Sets up a callback event
void NkTimeSetCbEvent (NkTimeEvent_t* event, NkTimeCallback cb, void* arg)
{
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkSpinLock (&event->lock);
    event->callback = cb;
    event->arg = arg;
    event->type = NEXKE_EVENT_CB;
    NkSpinUnlock (&event->lock);
    PltLowerIpl (ipl);
}

// Sets up a wakeup event
void NkTimeSetWakeEvent (NkTimeEvent_t* event, TskWaitObj_t* waiter)
{
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkSpinLock (&event->lock);
    event->waitObj = waiter;
    event->type = NEXKE_EVENT_WAKE;
    NkSpinUnlock (&event->lock);
    PltLowerIpl (ipl);
}

// Registers a time event
void NkTimeRegEvent (NkTimeEvent_t* event, ktime_t delta, int flags)
{
    // Raise IPL to protect event list
    NkCcb_t* ccb = CpuGetCcb();
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkSpinLock (&ccb->timeLock);
    NkSpinLock (&event->lock);
    if (event->inUse)
    {
        // Check if we are allowed to de register
        if (flags & NK_TIME_REG_DEREG)
            nkTimeEvtRemove (ccb, event);
        else
        {
            NkSpinUnlock (&event->lock);
            NkSpinUnlock (&ccb->timeLock);
            PltLowerIpl (ipl);
            return;    // Event expired, do nothing
        }
    }
    // Get deadline and convert delta to timer format
    event->deadline = NkTimeDeltaToDeadline (&delta);
    event->delta = delta;
    event->expired = false;
    // Check if this is a periodic register
    if (flags & NK_TIME_REG_PERIODIC)
        event->periodic = true;
    // Admit into queue
    nkTimeEvtAdmit (ccb, event, delta);
    // Return
    event->inUse = true;
    NkSpinUnlock (&event->lock);
    NkSpinUnlock (&ccb->timeLock);
    PltLowerIpl (ipl);
}

// Deregisters a time event
void NkTimeDeRegEvent (NkTimeEvent_t* event)
{
    // Raise IPL to protect event list
    NkCcb_t* ccb = CpuGetCcb();
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkSpinLock (&ccb->timeLock);
    NkSpinLock (&event->lock);
    if (event->expired || !event->inUse)
    {
        NkSpinUnlock (&event->lock);
        NkSpinUnlock (&ccb->timeLock);
        PltLowerIpl (ipl);
        return;    // Event expired, do nothing
    }
    // Remove it
    nkTimeEvtRemove (ccb, event);
    // Unlock and return
    NkSpinUnlock (&event->lock);
    NkSpinUnlock (&ccb->timeLock);
    PltLowerIpl (ipl);
}

// Drains timer queue
static FORCEINLINE void nkDrainTimeQueue (NkCcb_t* ccb, NkList_t* list, NkLink_t* iter)
{
    // We know a timer (or multiple) has/have expired, execute each cb
    NkTimeEvent_t* event = LINK_CONTAINER (iter, NkTimeEvent_t, link);
    ktime_t tick = event->deadline;
    while (iter && tick == event->deadline)
    {
        NkSpinLock (&event->lock);
        // Event has expired, remove from list and call handler
        event->inUse = false;
        NkLink_t* oldIter = iter;       // Save this iterator
        iter = NkListIterate (iter);    // To next one
        NkListRemove (list, oldIter);
        event->expired = true;    // Set expiry flag
        // Call the event handler
        if (event->type == NEXKE_EVENT_CB)
            event->callback (event, event->arg);
        else if (event->type == NEXKE_EVENT_WAKE)
        {
            // Wakeup this object
            bool wakeSuccess = TskClearWait (event->waitObj, TSK_WAITOBJ_TIMEOUT);
            if (wakeSuccess)
                TskWakeObj (event->waitObj);
        }
        // Check if this is a periodic event
        if (event->periodic)
        {
            // Make new deadline
            event->deadline = NkTimeDeltaToDeadline (&event->delta);
            event->expired = false;
            // Admit the new event
            nkTimeEvtAdmit (ccb, event, event->delta);
        }
        // Unlock and go to next event
        NkSpinUnlock (&event->lock);
        event = LINK_CONTAINER (iter, NkTimeEvent_t, link);    // To next one
    }
}

// Timer expiry handler
static void NkTimeHandler()
{
    NkCcb_t* ccb = CpuGetCcb();
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkSpinLock (&ccb->timeLock);
    NkList_t* list = &ccb->timeEvents;
    NkLink_t* iter = NkListFront (list);
    // If this is a software timer, we need to tick through the event until it expires
    // Otherwise, then an event has occured and we just need to execute each handler for this
    // deadline
    if (platform->timer->type == PLT_TIMER_SOFT)
    {
        // Check if timers have expired
        NkTimeEvent_t* event = LINK_CONTAINER (iter, NkTimeEvent_t, link);
        if (event->deadline == platform->clock->getTime())
            nkDrainTimeQueue (ccb, list, iter);
    }
    else
    {
        if (!iter)
            return;    // Return. no timer expired. Or assert maybe?
        nkDrainTimeQueue (ccb, list, iter);
        // Arm the next event
        NkLink_t* front = NkListFront (&ccb->timeEvents);
        if (front)
        {
            NkTimeEvent_t* event = LINK_CONTAINER (front, NkTimeEvent_t, link);
            NkSpinLock (&event->lock);
            int64_t delta = event->deadline - platform->clock->getTime();
            if (delta < 0)
            {
                // TODO: is there a better way of handling this?
                delta = 0;
            }
            platform->timer->armTimer (delta);
            ccb->nextDeadline = event->deadline;
            NkSpinUnlock (&event->lock);
        }
    }
    NkSpinUnlock (&ccb->timeLock);
    PltLowerIpl (ipl);
}

// Polls
void NkPoll (ktime_t time)
{
    ipl_t ipl = PltRaiseIpl (PLT_IPL_TIMER);
    platform->clock->poll (time);
    PltLowerIpl (ipl);
}

// Initializes timing subsystem
void NkInitTime()
{
    NkLogDebug ("nexke: intializing timer\n");
    platform = PltGetPlatform();
    NkListInit (&CpuGetCcb()->timeEvents);    // Initialize list
    nkEventCache = MmCacheCreate (sizeof (NkTimeEvent_t), "NkTimeEvent_t", 0, 0);
    // Set callback
    platform->timer->setCallback (NkTimeHandler);
}
