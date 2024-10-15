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

// Precision limiting factor
static uint64_t nkLimitPrecision = 0;

// Allocates a timer event
NkTimeEvent_t* NkTimeNewEvent()
{
    return MmCacheAlloc (nkEventCache);
}

// Frees a timer event
void NkTimeFreeEvent (NkTimeEvent_t* event)
{
    assert (!event->link.next && !event->link.prev);
    MmCacheFree (nkEventCache, event);
}

// Converts a delta to a deadline
static uint64_t NkTimeDeltaToDeadline (uint64_t* delta)
{
    // Get ref tick
    uint64_t refTick = platform->clock->getTime();
    uint64_t deadline = refTick + *delta;
    // If deadline equals ref tick (i.e., delta is 0) increase it by one tick
    if (refTick == deadline)
    {
        ++*delta;
        ++deadline;
    }
    return deadline;
}

// Registers a time event
void NkTimeRegEvent (NkTimeEvent_t* event, uint64_t delta, NkTimeCallback callback, void* arg)
{
    if (event->inUse)
        return;    // Don't register a in use event
    // Setup event
    event->arg = arg;
    event->callback = callback;
    // Raise IPL to protect event list
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    // Get deadline and convert delta to timer format
    event->deadline = NkTimeDeltaToDeadline (&delta);
    // Ensure deadline is valid for this timer
    // Grab list
    NkCcb_t* ccb = CpuGetCcb();
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
            if (event->deadline < cur->deadline || !iter->next)
            {
                // Found spot, add it here
                NkListAdd (list, iter, &event->link);
                break;    // Exit loop
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
        }
    }
    event->inUse = true;
    PltLowerIpl (ipl);
}

// Deregisters a time event
void NkTimeDeRegEvent (NkTimeEvent_t* event)
{
    // Raise IPL to protect event list
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkCcb_t* ccb = CpuGetCcb();
    NkList_t* list = &ccb->timeEvents;
    // Figure out if this is the head
    bool isHead = false;
    if (&event->link == NkListFront (list))
        isHead = true;
    // Remove this event from list
    NkListRemove (list, &event->link);
    // Set head if needed
    if (isHead)
    {
        // Now we need to re-arm the timer
        if (platform->timer->type != PLT_TIMER_SOFT)
        {
            uint64_t deadline = event->deadline;
            int64_t delta = deadline - platform->clock->getTime();
            if (delta < 0)
            {
                // TODO: is there a better way of handling this?
                delta = 0;
            }
            platform->timer->armTimer (delta);
        }
    }
    event->link.next = event->link.prev = NULL;
    PltLowerIpl (ipl);
}

// Timer expiry handler
static void NkTimeHandler()
{
    NkCcb_t* ccb = CpuGetCcb();
    NkList_t* list = &ccb->timeEvents;
    NkLink_t* iter = NkListFront (list);
    NkTimeEvent_t* event = LINK_CONTAINER (iter, NkTimeEvent_t, link);
    // If this is a software timer, we need to tick through the event until it expires
    // Otherwise, then an event has occured and we just need to execute each handler for this
    // deadline
    if (platform->timer->type == PLT_TIMER_SOFT)
    {
        // Check if timers have expired
        while (iter && platform->clock->getTime() == event->deadline)
        {
            // Event has expired, remove from list and call handler
            NkLink_t* oldIter = iter;       // Save this iterator
            iter = NkListIterate (iter);    // To next one
            NkListRemove (list, oldIter);
            event->callback (event, event->arg);
            event = LINK_CONTAINER (iter, NkTimeEvent_t, link);    // To next one
        }
    }
    else
    {
        if (!event)
            return;    // Return. no timer expired. Or assert maybe?
        // We know a timer (or multiple) has/have expired, execute each cb
        uint64_t tick = event->deadline;
        while (iter && tick == event->deadline)
        {
            // Event has expired, remove from list and call handler
            NkLink_t* oldIter = iter;       // Save this iterator
            iter = NkListIterate (iter);    // To next one
            NkListRemove (list, oldIter);
            event->callback (event, event->arg);
            event = LINK_CONTAINER (iter, NkTimeEvent_t, link);    // To next one
        }
        // Arm the next event
        NkLink_t* front = NkListFront (&ccb->timeEvents);
        if (front)
        {
            event = LINK_CONTAINER (front, NkTimeEvent_t, link);
            int64_t delta = event->deadline - platform->clock->getTime();
            if (delta < 0)
            {
                // TODO: is there a better way of handling this?
                delta = 0;
            }
            platform->timer->armTimer (delta);
        }
    }
}

// Polls
void NkPoll (uint64_t time)
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
    // Set limiting precision
    if (platform->clock->precision > platform->timer->precision)
        nkLimitPrecision = platform->clock->precision;
    else
        nkLimitPrecision = platform->timer->precision;
    NkListInit (&CpuGetCcb()->timeEvents);    // Initialize list
    nkEventCache = MmCacheCreate (sizeof (NkTimeEvent_t), "NkTimeEvent_t", 0, 0);
    // Set callback
    platform->timer->setCallback (NkTimeHandler);
}
