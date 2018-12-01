# Canvas SDK

## Overview
TODO...

## Interfaces
Canvas uses an interface model similar to ATL COM.  All Canvas objects inherit from IGeneric (similar to IUnknown in COM), providing access to other available interfaces using IGeneric::QueryInterface.

Like COM, interfaces are identified using a unique ID.  However, rather than a globally unique 256-bit guid, Canvas interfaces are identified using a locally-unique 32-bit unsigned integer, a little faster for QI implementation. Internal interface identifiers can range from 0x00000001 to 0x7fffffff. Custom interfaces identifiers can be in the range of 0x80000000 to 0xffffffff.  The identifier 0x00000000 is reserved.

Canvas interface objects are reference-counted.  Objects are created with a reference count of 1.  Object references are incremented by IObject::AddRef or by a successful IObject::QueryInterface call.  IObject::Release decrements the objects reference count.  When the reference count reaches zero the object is freed.

## Canvas Interface Rules
* QI increments the reference count on the IGeneric implementor
* All QI for IGeneric must return the same address for all interface objects queried from the same initial interface
* If A can QI for B and B can QI for C then A can QI for C (and so on...)
* Interfaces must be fully committed at object creation
  * QI must **always** succeed or **always** fail for a given interface id

