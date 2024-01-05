/*
    object.c - contains object database management
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

#include <assert.h>
#define NEED_SVC_PTRS
#include <nexboot/nexboot.h>
#include <string.h>

// Object data structure
typedef struct _objDir
{
    int childCount;           /// Number of child objects
    NbObject_t* childList;    /// List of children
} objDir_t;

// Root directory pointer
static NbObject_t* rootDir = NULL;

extern NbObjSvcTab_t objDirSvcs;

// Pathname parser
typedef struct _pathpart
{
    const char* oldName;    // Original name
    char name[80];          // Component name
    bool isLastPart;        // Is this the last part?
} pathPart_t;

static void _parsePath (pathPart_t* part)
{
    memset (part->name, 0, 80);
    // Check if we need to skip over a '/'
    if (*part->oldName == '/')
        ++part->oldName;
    // Copy characters name until we reach a '/'
    int i = 0;
    while (*part->oldName != '/' && *part->oldName != '\0')
    {
        part->name[i] = *part->oldName;
        ++i;
        ++part->oldName;
    }
    // If we reached a null terminator, finish
    if (*part->oldName == '\0')
        part->isLastPart = true;
}

// Gets base name of path
static const char* _basename (const char* name)
{
    // Goto end of string
    name += strlen (name);
    while (*(name - 1) != '/')
        --name;
    return name;
}

// Inserts an object in the tree
static bool nbInsertObj (const char* name, NbObject_t* obj)
{
    // Parse the path
    pathPart_t part;
    memset (&part, 0, sizeof (pathPart_t));
    part.oldName = name;
    bool finished = false;
    NbObject_t* curDir = rootDir;
    while (!finished)
    {
        _parsePath (&part);
        // If this is not the last component, then find next component in directory
        if (!part.isLastPart)
        {
            // Make sure this is a directory
            if (curDir->type != OBJ_TYPE_DIR)
            {
                // That's an error
                return false;
            }
            ObjDirOp_t op;
            op.name = part.name;
            bool res = NbObjCallSvc (curDir, OBJDIR_FIND_CHILD, &op);
            if (!res)
            {
                // Directory part doesn't exist, that's an error
                return false;
            }
            curDir = op.foundObj;
        }
        else
        {
            // Add to curDir
            ObjDirOp_t op;
            op.obj = obj;
            NbObjCallSvc (curDir, OBJDIR_ADD_CHILD, &op);
            finished = true;
        }
    }
    return true;
}

NbObject_t* NbObjCreate (const char* name, int type, int interface)
{
    // Check if object exists
    if (rootDir)
    {
        if (NbObjFind (name))
            return NULL;
    }
    // Allocate a new object
    NbObject_t* obj = (NbObject_t*) calloc (1, sizeof (NbObject_t));
    if (!obj)
        return NULL;
    ObjCreate ("NbObject_t", &obj->obj);
    obj->interface = interface;
    obj->type = type;
    strcpy (obj->name, _basename (name));
    // If this is a directory, go ahead and install the services interface
    if (obj->type == OBJ_TYPE_DIR)
        NbObjInstallSvcs (obj, &objDirSvcs);
    // Add method to tree
    // Edge case: if name == "", we are the root directory and must set that variable
    if (!strcmp (name, "/"))
        rootDir = obj;
    else
        nbInsertObj (name, obj);
    return obj;
}

void NbObjInitDb()
{
    rootDir = NULL;
    NbObjCreate ("/", OBJ_TYPE_DIR, OBJ_INTERFACE_DIR);
}

NbObject_t* NbObjFind (const char* name)
{
    // Edge case: if name is "/", return rootDir
    if (!strcmp (name, "/"))
        return rootDir;
    // Parse path into components
    pathPart_t part;
    memset (&part, 0, sizeof (pathPart_t));
    part.oldName = name;
    NbObject_t* curDir = rootDir;
    while (1)
    {
        _parsePath (&part);
        // Find component in directory
        ObjDirOp_t op = {0};
        op.name = part.name;
        bool res = NbObjCallSvc (curDir, OBJDIR_FIND_CHILD, &op);
        if (!res)
            return NULL;    // Object doesn't exist
        if (part.isLastPart)
            return op.foundObj;
        curDir = op.foundObj;
    }
}

NbObject_t* NbObjRef (NbObject_t* obj)
{
    assert (obj);
    ObjRef (&obj->obj);
    if (obj->services[OBJ_SERVICE_REF])
        obj->services[OBJ_SERVICE_REF](obj, NULL);
    return obj;
}

void NbObjDeRef (NbObject_t* obj)
{
    assert (obj);
    if (!ObjDeRef (&obj->obj))
    {
        // Remove us from parent directory
        assert (obj->parent->type == OBJ_TYPE_DIR);
        ObjDirOp_t op;
        op.obj = obj;
        NbObjCallSvc (obj->parent, OBJDIR_REMOVE_CHILD, &op);
        if (obj->services[OBJ_SERVICE_DESTROY])
            obj->services[OBJ_SERVICE_DESTROY](obj, NULL);
        free (obj);
    }
}

bool NbObjCallSvc (NbObject_t* obj, int svc, void* svcArgs)
{
    assert (obj);
    if (svc >= obj->numSvcs)
        return false;
    return obj->services[svc](obj, svcArgs);
}

void NbObjInstallSvcs (NbObject_t* obj, NbObjSvcTab_t* svcTab)
{
    assert (obj && svcTab);
    obj->services = svcTab->svcTab;
    obj->numSvcs = svcTab->numSvcs;
    assert (svcTab->svcTab[3] && svcTab->svcTab[4]);
    // Call init service
    if (obj->services[OBJ_SERVICE_INIT])
        NbObjCallSvc (obj, OBJ_SERVICE_INIT, NULL);
}

NbObject_t* NbObjEnumDir (NbObject_t* dir, NbObject_t* iter)
{
    if (dir->type != OBJ_TYPE_DIR)
        return NULL;
    ObjDirOp_t op;
    op.enumStat = iter;
    NbObjCallSvc (dir, OBJDIR_ENUM_CHILD, &op);
    return op.enumStat;
}

char* NbObjGetPath (NbObject_t* obj, char* buf, size_t bufSz)
{
    NbObject_t* iter = obj;
    size_t totalLen = 1;
    memset (buf, 0, bufSz);
    char* curBuf = buf + (bufSz - 1);
    // Go backwards
    while (iter->parent)
    {
        // Find out length of this part, accounting for slash
        int len = strlen (iter->name) + 1;
        totalLen += len;
        if (totalLen > bufSz)    // Overflow check
            return NULL;
        curBuf -= len;
        *curBuf = '/';                               // Add seperator
        memcpy (curBuf + 1, iter->name, len - 1);    // Copy out component
        iter = iter->parent;
    }
    return curBuf;
}

// Initializes object directory
static bool nbObjDirInit (void* dirp, void* unused)
{
    NbObject_t* dir = dirp;
    assert (dir);
    // Allocate directory structure
    objDir_t* dirData = (objDir_t*) malloc (sizeof (objDir_t));
    if (!dirData)
        return false;
    dirData->childCount = 0;
    dirData->childList = NULL;
    dir->data = dirData;
    return true;
}

static bool nbObjDirRef (void* dir, void* unused)
{
    return true;
}

// Destroy an object directory
static bool nbObjDirDestroy (void* dirp, void* unused)
{
    NbObject_t* dir = dirp;
    assert (dir);
    // Ensure we are child free.
    // NOTE: we do an assert because if child count is > 0, than we still have a
    // reference somewhere
    objDir_t* dirData = dir->data;
    assert (!dirData->childCount);
    free (dir->data);
    return true;
}

// Adds a child object to an object directory
static bool nbObjAddChild (void* dirp, void* opp)
{
    NbObject_t* dir = dirp;
    ObjDirOp_t* op = opp;
    assert (dir && op);
    // Get object to add
    NbObject_t* obj = op->obj;
    assert (obj);
    // NOTE: currently we don't sort objects
    // I don't see much of a need to, hence, we just add this one to the front
    // Add object to childList
    objDir_t* dirData = dir->data;
    obj->nextChild = dirData->childList;
    obj->prevChild = NULL;
    if (dirData->childList)
        dirData->childList->prevChild = obj;
    dirData->childList = obj;
    dirData->childCount++;
    // Set parent
    obj->parent = NbObjRef (dir);
    return true;
}

// Removes a child object from an object directory
static bool nbObjRemoveChild (void* dirp, void* opp)
{
    NbObject_t* dir = dirp;
    ObjDirOp_t* op = opp;
    assert (dir && op);
    NbObject_t* obj = op->obj;
    objDir_t* dirData = dir->data;
    // Ensure this object is our child
    if (obj->parent != dir)
    {
        op->status = OBJDIR_ERR_NOT_CHILD;
        return false;
    }
    // Ensure object isn't another directory
    if (obj->type == OBJ_TYPE_DIR)
    {
        // Check if this directory has any children
        objDir_t* dirData2 = obj->data;
        if (dirData2->childCount != 0)
        {
            op->status = OBJDIR_ERR_DIR_NOT_EMPTY;
            return false;
        }
    }
    // Remove object from childList
    if (obj->prevChild)
        obj->prevChild->nextChild = obj->nextChild;
    if (obj->nextChild)
        obj->nextChild->prevChild = obj->prevChild;
    if (obj == dirData->childList)
        dirData->childList = obj->nextChild;
    NbObjDeRef (obj->parent);
    dirData->childCount--;
    return true;
}

// Finds a child in an object directory
static bool nbObjFindChild (void* dirp, void* opp)
{
    NbObject_t* dir = dirp;
    ObjDirOp_t* op = opp;
    assert (dir && op);
    // Get child list
    objDir_t* dirData = dir->data;
    NbObject_t* curChild = dirData->childList;
    const char* name = op->name;
    while (curChild)
    {
        // Compare name
        if (!strcmp (curChild->name, name))
        {
            op->foundObj = curChild;
            return true;
        }
        curChild = curChild->nextChild;
    }
    op->status = OBJDIR_ERR_OBJ_NOT_FOUND;
    return false;
}

// Enumerates child directory
static bool nbObjEnumChild (void* dirp, void* opp)
{
    NbObject_t* dir = dirp;
    objDir_t* dirData = dir->data;
    ObjDirOp_t* op = opp;
    assert (dir && op);
    // If enumeration has started, just go to next
    if (op->enumStat)
        op->enumStat = op->enumStat->nextChild;
    else
        op->enumStat = dirData->childList;    // Else start enumeration
    return true;
}

static bool nbObjDirDump (void* dir, void* unused)
{
    return true;
}

static bool nbObjDirNotify (void* dir, void* unused)
{
    return true;
}

// Object directory interface
static NbObjSvc objDirFuncs[] = {nbObjDirInit,
                                 nbObjDirRef,
                                 nbObjDirDestroy,
                                 nbObjDirDump,
                                 nbObjDirNotify,
                                 nbObjAddChild,
                                 nbObjRemoveChild,
                                 nbObjFindChild,
                                 nbObjEnumChild};

NbObjSvcTab_t objDirSvcs = {ARRAY_SIZE (objDirFuncs), objDirFuncs};
