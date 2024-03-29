/*
    object.h - contains object functions and structures
    Copyright 2023 - 2024 The NexNix Project

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

#ifndef _NB_OBJECT_H
#define _NB_OBJECT_H

#include <libnex/object.h>
#include <nexboot/driver.h>
#include <nexboot/object_types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define OBJ_ISVALID(obj) ((obj)->name)

typedef bool (*NbObjSvc) (void*, void*);

/// Object structure
typedef struct _obj
{
    Object_t obj;              /// libnex object for reference counting
    char name[64];             /// Name of object in hierarchy
    int type;                  /// Type of object
    int interface;             /// Interface of object
    NbObjSvc* services;        /// Object services
    size_t numSvcs;            /// The number of services
    void* data;                /// Pointer to object data
    struct _obj* parent;       /// Parent object
    struct _obj* nextChild;    /// Next child object
    struct _obj* prevChild;    /// Previous child object
    NbDriver_t* owner;         /// Owner of this object
    NbDriver_t* manager;       /// Managing driver
} NbObject_t;

typedef struct _svcTab
{
    size_t numSvcs;
    NbObjSvc* svcTab;
} NbObjSvcTab_t;

/// Standard interface services
#define OBJ_SERVICE_INIT     0
#define OBJ_SERVICE_DESTROY  2
#define OBJ_SERVICE_REF      1
#define OBJ_SERVICE_DUMPDATA 3
#define OBJ_SERVICE_NOTIFY   4

typedef struct _objNotify
{
    int code;
    void* data;
} NbObjNotify_t;

/// Initializes object database
void NbObjInitDb();

/// Creates a new object
NbObject_t* NbObjCreate (const char* name, int type, int interface);

/// References an object
NbObject_t* NbObjRef (NbObject_t* obj);

/// De-references an object, destroying if refcount = 0
void NbObjDeRef (NbObject_t* obj);

/// Finds an object in the object tree
NbObject_t* NbObjFind (const char* name);

/// Calls an object service
bool NbObjCallSvc (NbObject_t* obj, int svc, void* svcArgs);

/// Installs service table pointer
void NbObjInstallSvcs (NbObject_t* obj, NbObjSvcTab_t* svcTab);

/// Enumerates a child directory
NbObject_t* NbObjEnumDir (NbObject_t* dir, NbObject_t* iter);

/// Gets path to object
char* NbObjGetPath (NbObject_t* obj, char* buf, size_t bufSz);

/// Get object interface
#define NbObjGetInterface(obj) ((obj)->interface)

/// Get object type
#define NbObjGetType(obj) ((obj)->type)

/// Set object data
#define NbObjSetData(obj, datap) ((obj)->data = (datap))

/// Get object data
#define NbObjGetData(obj) ((obj)->data)

/// Set owner of object
#define NbObjSetOwner(obj, nowner) ((obj)->owner = (nowner))

/// Get owner of object
#define NbObjGetOwner(obj) ((obj)->owner)

/// Set manager of object
#define NbObjSetManager(obj, nmanager) ((obj)->manager = (nmanager))

/// Get manager of object
#define NbObjGetManager(obj) ((obj)->manager)

#define OBJDIR_ADD_CHILD    5
#define OBJDIR_REMOVE_CHILD 6
#define OBJDIR_FIND_CHILD   7
#define OBJDIR_ENUM_CHILD   8

// Method structure
typedef struct _objdirOp
{
    union
    {
        struct
        {
            NbObject_t* foundObj;
            const char* name;
        };
        NbObject_t* obj;
        NbObject_t* enumStat;
    };
    int status;
} ObjDirOp_t;

// Error statuses
#define OBJDIR_ERR_NOT_CHILD     1
#define OBJDIR_ERR_DIR_NOT_EMPTY 2
#define OBJDIR_ERR_OBJ_NOT_FOUND 3

#endif
