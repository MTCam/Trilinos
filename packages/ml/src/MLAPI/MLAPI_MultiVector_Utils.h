#ifndef MLAPI_DOUBLEVECTOR_UTILS_H
#define MLAPI_DOUBLEVECTOR_UTILS_H

#include "MLAPI_Error.h"
#include "MLAPI_MultiVector.h"

namespace MLAPI {

/*!
\file MLAPI_MultiVector_Utils.h

\brief Utilities for MultiVector's.

\author Marzio Sala, SNL 9214.

\date Last updated on Feb-05.
*/

//! Creates a new vector, x, such that x = y.
MultiVector Duplicate(const MultiVector& y);

//! Creates a new vector, x, such that x = y(:,v)
MultiVector Duplicate(const MultiVector& y, const int v);

//! Extracts a component from a vector.
MultiVector Extract(const MultiVector& y, const int v);

//! Redistributes the entry of a vector as a multivector.
MultiVector Redistribute(const MultiVector& y, const int NumEquations);

} // namespace MLAPI

#endif
