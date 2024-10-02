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
    assert (!event->next && !event->prev && CpuGetCcb()->timeEvents != event);
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
    NkTimeEvent_t* list = ccb->timeEvents;
    // Find where in list to add
    // Basically the list is sorted by when event deadline is. Earliest deadline is first
    if (!list)
    {
        // List is empty, add to front
        ccb->timeEvents = event;
        event->next = event->prev = NULL;
    }
    else
    {
        // Loop through list to find spot
        while (list)
        {
            if (event->deadline < list->deadline)
            {
                // Found spot, add it here
                if (list->prev)
                    list->prev->next = event;
                list->prev = event;
                event->next = list;
                if (ccb->timeEvents == list)
                {
                    // Set this to front pointer
                    ccb->timeEvents = list;
                }
                break;    // Exit loop
            }
            // Handle case of list ending after this entry
            if (list->next == NULL)
            {
                // List ends after this, which means this is the latest event currently
                // Add it to tail
                list->next = event;
                event->prev = list;
                event->next = NULL;
                break;
            }
            list = list->next;    // To next spot
        }
    }
    // If this event is in the front, then we need to arm the timer
    // NOTE: this is only true if we are not using a software timer.
    // In that case, we have no need to arm anything
    if (event == ccb->timeEvents)
    {
        // Arm timer if needed
        if (platform->timer->type != PLT_TIMER_SOFT)
        {
            // Arm timer
            platform->timer->armTimer (delta);
        }
    }
    PltLowerIpl (ipl);
}

// Deregisters a time event
void NkTimeDeRegEvent (NkTimeEvent_t* event)
{
    // Raise IPL to protect event list
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkCcb_t* ccb = CpuGetCcb();
    NkTimeEvent_t* list = ccb->timeEvents;
    assert (event->prev || event->next || list == event);
    // Remove this event from list
    if (event->prev)
        event->prev->next = event->next;
    if (event->next)
        event->next->prev = event->prev;
    // Set head if needed
    if (list == event)
    {
        ccb->timeEvents = event->next;
        // Now we need to re-arm the timer
        if (platform->timer->type != PLT_TIMER_SOFT)
        {
            uint64_t deadline = event->deadline;
            uint64_t delta = deadline - platform->clock->getTime();
            platform->timer->armTimer (delta);
        }
    }
    event->next = event->prev = NULL;
    PltLowerIpl (ipl);
}

// Timer expiry handler
static void NkTimeHandler()
{
    NkCcb_t* ccb = CpuGetCcb();
    NkTimeEvent_t* event = ccb->timeEvents;
    // If this is a software timer, we need to tick through the event until it expires
    // Otherwise, then an event has occured and we just need to execute each handler for this
    // deadline
    if (platform->timer->type == PLT_TIMER_SOFT)
    {
        // Check if timers have expired
        while (event && platform->clock->getTime() == event->deadline)
        {
            // Event has expired, remove from list and call handler
            ccb->timeEvents = event->next;
            event->callback (event, event->arg);
            event = event->next;    // To next event
        }
    }
    else
    {
        if (!event)
            return;    // Return. no timer expired. Or assert maybe?
        // We know a timer (or multiple) has/have expired, execute each cb
        uint64_t tick = event->deadline;
        while (event && tick == event->deadline)
        {
            // Event has expired, remove from list and call handler
            ccb->timeEvents = event->next;
            event->callback (event, event->arg);
            event = event->next;    // To next event
        }
    }
}

// Initializes timing subsystem
void NkInitTime()
{
    platform = PltGetPlatform();
    // Set limiting precision
    if (platform->clock->precision > platform->timer->precision)
        nkLimitPrecision = platform->clock->precision;
    else
        nkLimitPrecision = platform->timer->precision;
    // Set up callback
    CpuGetCcb()->timeEvents = NULL;    // Initialize list
    nkEventCache = MmCacheCreate (sizeof (NkTimeEvent_t), NULL, NULL);
    // Set callback
    platform->timer->setCallback (NkTimeHandler);
}
