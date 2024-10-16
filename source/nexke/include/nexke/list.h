/*
    list.h - contains list implementation
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

#ifndef _NK_LIST_H
#define _NK_LIST_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

// List link definition
typedef struct _link
{
    struct _link* prev;
    struct _link* next;
} NkLink_t;

// List head
typedef struct _list
{
    NkLink_t* head;
    NkLink_t* tail;
} NkList_t;

// Gets the containter for a link
#define LINK_CONTAINER(addr, type, field) \
    ((type*) ((uintptr_t) (addr) - (uintptr_t) (&((type*) 0)->field)))

// Initializes a list
static inline void NkListInit (NkList_t* list)
{
    list->head = 0, list->tail = 0;
}

// Adds item to front of list
static inline void NkListAddFront (NkList_t* list, NkLink_t* item)
{
    item->next = list->head;
    item->prev = NULL;
    if (list->head)
        list->head->prev = item;
    list->head = item;
    if (!list->tail)
        list->tail = item;
}

// Adds item to back of list
static inline void NkListAddBack (NkList_t* list, NkLink_t* item)
{
    item->prev = list->tail;
    item->next = NULL;
    if (list->tail)
        list->tail->next = item;
    list->tail = item;
    if (!list->head)
        list->head = item;
}

// Adds item after item
static inline void NkListAdd (NkList_t* list, NkLink_t* item, NkLink_t* newItem)
{
    if (item->next)
        item->next->prev = newItem;
    item->next = newItem;
    newItem->prev = item;
    if (item == list->tail)
        list->tail = newItem;
}

// Adds item before item
static inline void NkListAddBefore (NkList_t* list, NkLink_t* item, NkLink_t* newItem)
{
    if (item->prev)
        item->prev->next = newItem;
    item->prev = newItem;
    newItem->next = item;
    if (item == list->head)
        list->head = newItem;
}

// Removes item from list
static inline void NkListRemove (NkList_t* list, NkLink_t* item)
{
    if (item->next)
        item->next->prev = item->prev;
    if (item->prev)
        item->prev->next = item->next;
    if (item == list->head)
        list->head = item->next;
    if (item == list->tail)
        list->tail = item->prev;
}

// Takes item from front of list
static inline NkLink_t* NkListPopFront (NkList_t* list)
{
    NkLink_t* ret = list->head;
    if (ret)
    {
        list->head = ret->next;
        if (list->head)
            list->head->prev = NULL;
        else if (ret == list->tail)
            list->tail = list->head;
    }
    return ret;
}

// Gets first item in list
static inline NkLink_t* NkListFront (NkList_t* list)
{
    return list->head;
}

// Iterates to next item in list
static inline NkLink_t* NkListIterate (NkLink_t* link)
{
    return link->next;
}

#endif
