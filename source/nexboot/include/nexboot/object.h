/*
    object.h - contains object functions and structures
    Copyright 2023 The NexNix Project

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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define OBJ_ISVALID(obj) ((obj)->name)

typedef bool (*NbObjSvc) (void*, void*);

/// Object structure
typedef struct _obj
{
    Object_t obj;              /// libnex object for reference counting
    const char* name;          /// Name of object in hierarchy
    int type;                  /// Type of object
    int interface;             /// Interface of object
    NbObjSvc* services;        /// Object services
    size_t numSvcs;            /// The number of services
    void* data;                /// Pointer to object data
    struct _obj* parent;       /// Parent object
    struct _obj* nextChild;    /// Next child object
    struct _obj* prevChild;    /// Previous child object
} NbObject_t;

#include <nexboot/services.h>

/// Standard interface services
#define OBJ_SERVICE_INIT    0
#define OBJ_SERVICE_DESTROY 2
#define OBJ_SERVICE_REF     1

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

/// Get object interface
#define NbObjGetInterface(obj) ((obj)->interface)

/// Get object type
#define NbObjGetType(obj) ((obj)->type)

/// Set object data
#define NbObjSetData(obj, data) ((obj)->data = (data))

/// Get object data
#define NbObjGetData(obj) ((obj)->data)

/// Object directory interfaces
#define OBJ_TYPE_DIR      0
#define OBJ_INTERFACE_DIR 0

#define OBJDIR_ADD_CHILD    3
#define OBJDIR_REMOVE_CHILD 4
#define OBJDIR_FIND_CHILD   5

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
    };
    int status;
} ObjDirOp_t;

// Error statuses
#define OBJDIR_ERR_NOT_CHILD     1
#define OBJDIR_ERR_DIR_NOT_EMPTY 2
#define OBJDIR_ERR_OBJ_NOT_FOUND 3

#endif
