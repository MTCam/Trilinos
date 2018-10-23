/*
// @HEADER
// ***********************************************************************
//
//          Tpetra: Templated Linear Algebra Services Package
//                 Copyright (2008) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Michael A. Heroux (maherou@sandia.gov)
//
// ************************************************************************
// @HEADER
*/

#include <Tpetra_TestingUtilities.hpp>
#include <Tpetra_Core.hpp>
#include <Tpetra_Map.hpp>
#include <Tpetra_MultiVector.hpp>
#include <Tpetra_Vector.hpp>
#include <Kokkos_ArithTraits.hpp>
#include <Teuchos_DefaultSerialComm.hpp>

namespace Harness {

  enum AccessMode {
    ReadOnly,
    WriteOnly,
    ReadWrite
  };

  namespace Impl {
    // Given a global object, get its default memory space (both the
    // type and the default instance thereof).
    template<class GlobalObjectType>
    struct DefaultMemorySpace {
      using type = typename GlobalObjectType::memory_space;

      // Given a global object, get its (default) memory space instance.
      static type space (const GlobalObjectType& /* G */) {
	// This stub just assumes that 'type' is default constructible.
	// In Kokkos, default-constructing a memory space instance just
	// gives the default memory space.
	return type ();
      }
    };

    // Struct that tells withLocalAccess how to access a global object's
    // local data.  Do not use this directly; start with readOnly,
    // writeOnly, or readWrite.
    template<class GlobalObjectType,
	     class MemorySpace,
	     const AccessMode am>
    class LocalAccess; // forward declaration

    // Mapping from LocalAccess to the "master" local object type.  The
    // latter gets the local data from a global object, and holds on to
    // it until after the user's function (input to withLocalAccess)
    // returns.
    template<class LocalAccessType>
    struct GetMasterLocalObject {};

    // Given a LocalAccess instance (which has a reference to a global
    // object), get an instance of its master local object.  This may be
    // a heavyweight operation.
    //
    // If you need to specialize this, just specialize get() in
    // GetMasterLocalObject above.
    template<class LocalAccessType>
    typename GetMasterLocalObject<LocalAccessType>::master_local_object_type
    getMasterLocalObject (LocalAccessType LA) {
      return GetMasterLocalObject<LocalAccessType>::get (LA);
    }

    // Mapping from "master" local object type to the nonowning "local
    // view" type that users see (as arguments to the function that they
    // give to withLocalAccess).  The master local object may encode the
    // memory space and access mode, but the mapping to local view type
    // may also need run-time information.
    template<class MasterLocalObjectType>
    struct GetNonowningLocalObject {};

    // Given a master local object, get an instance of a nonowning local
    // object.  Users only ever see the nonowning local object, and
    // subviews (slices) thereof.  This is supposed to be a lightweight
    // operation.
    //
    // If you need to specialize this, just specialize get() in
    // GetNonowningLocalObject above.
    template<class MasterLocalObjectType>
    typename GetNonowningLocalObject<MasterLocalObjectType>::nonowning_local_object_type
    getNonowningLocalObject (const MasterLocalObjectType& master) {
      return GetNonowningLocalObject<MasterLocalObjectType>::get (master);
    }

    // Use the LocalAccess type as the template parameter to determine
    // the type of the nonowning local view to the global object's data.
    // This only works if GetMasterLocalObject has been specialized for
    // these template parameters, and if GetNonowningLocalObject has
    // been specialized for the resulting "master" local object type.
    template<class LocalAccessType>
    class LocalAccessFunctionArgument {
    private:
      using gmlo = GetMasterLocalObject<LocalAccessType>;
      using master_local_object_type = typename gmlo::master_local_object_type;
      using gnlo = GetNonowningLocalObject<master_local_object_type>;
    public:
      using type = typename gnlo::nonowning_local_object_type;
    };
  } // namespace Impl

  //////////////////////////////////////////////////////////////////////
  // Users call readOnly, writeOnly, and readWrite, in order to declare
  // how they intend to access a global object's local data.
  //////////////////////////////////////////////////////////////////////

  // Declare that you want to access the given global object's local
  // data in read-only mode.
  template<class GlobalObjectType>
  Impl::LocalAccess<GlobalObjectType,
		    typename Impl::DefaultMemorySpace<GlobalObjectType>::type,
		    ReadOnly>
  readOnly (GlobalObjectType&);

  // Declare that you want to access the given global object's local
  // data in write-only mode.
  template<class GlobalObjectType>
  Impl::LocalAccess<GlobalObjectType,
		    typename Impl::DefaultMemorySpace<GlobalObjectType>::type,
		    WriteOnly>
  writeOnly (GlobalObjectType&);

  // Declare that you want to access the given global object's local
  // data in read-and-write mode.
  template<class GlobalObjectType>
  Impl::LocalAccess<GlobalObjectType,
		    typename Impl::DefaultMemorySpace<GlobalObjectType>::type,
		    ReadWrite>
  readWrite (GlobalObjectType&);

  namespace Impl {
    // Declaration of access intent for a global object.
    //
    // Users aren't supposed to make instances of this class.  They
    // should use readOnly, writeOnly, or readWrite instead, then call
    // methods like on() and valid() on the resulting LocalAccess
    // instance.
    template<class GlobalObjectType,
	     class MemorySpace,
	     const AccessMode am>
    class LocalAccess {
    public:
      using global_object_type = GlobalObjectType;
      using memory_space = typename MemorySpace::memory_space;    
      static constexpr AccessMode access_mode = am;

      // Users must NOT call the LocalAccess constructor directly.  They
      // should instead start by calling readOnly, writeOnly, or
      // readWrite above.  They may then use methods like on() or
      // valid() (see below).
      //
      // G is a reference, because we only access it in a delimited
      // scope.  G is nonconst, because even read-only local access may
      // modify G.  For example, G may give access to its local data via
      // lazy allocation of a data structure that differs from its
      // normal internal storage format.
      //
      // Memory spaces should behave like Kokkos memory spaces.  Default
      // construction should work and should get the default instance of
      // the space.  Otherwise, it may make sense to get the default
      // memory space from G.
      LocalAccess (global_object_type& G,
		   memory_space space = memory_space (),
		   const bool isValid = true) :
	G_ (G),
	space_ (space),
	valid_ (isValid)
      {}

      // Type users see, that's an argument to the function that they give
      // to withLocalAccess.
      using function_argument_type =
	typename LocalAccessFunctionArgument<LocalAccess<global_object_type, memory_space, access_mode> >::type;

    public:
      // Give users run-time control over whether they actually want to
      // access the object.  If isValid is false, implementations should
      // not spend any effort getting the master local object.  This may
      // save time on allocating temporary space, copying from device to
      // host, etc.  This implies that implementations must be able to
      // construct "null" / empty master local objects.
      LocalAccess<GlobalObjectType, MemorySpace, am>
      valid (const bool isValid) const {
	std::cout << "  .valid(" << (isValid ? "true" : "false") << ")" << std::endl;
	return {this->G_, this->space_, isValid};
      }

      // Let users access this object in a different memory space.
      //
      // NOTE: This also works for PGAS.  'space' in that case could be
      // something like an MPI_Win, or a UPC-style "shared pointer."
      //
      // NewMemorySpace should behave like a Kokkos memory space
      // instance.
      template<class NewMemorySpace>
      LocalAccess<GlobalObjectType, NewMemorySpace, am>
      on (NewMemorySpace space) const {
	std::cout << "  .on(" << space.name () << ")" << std::endl;
	return {this->G_, space, this->valid_};
      }

      // Is access supposed to be valid?  (See valid() above.)
      bool isValid () const { return this->valid_; }

      memory_space getSpace () const { return space_; }
  
    public:
      // Keep by reference, because this struct is only valid in a
      // delimited scope.
      global_object_type& G_;
      memory_space space_; // assume shallow-copy semantics
      bool valid_; // will I actually need to access this object?

    private:
      // Nonmember "constructors"; see above for declarations.  This are
      // friends, because they are the only ways that users are supposed
      // to construct LocalAccess instances.
      template<class GOT> friend
      LocalAccess<GOT, typename Impl::DefaultMemorySpace<GOT>::type, ReadOnly> readOnly (GOT&);
      template<class GOT> friend
      LocalAccess<GOT, typename Impl::DefaultMemorySpace<GOT>::type, WriteOnly> writeOnly (GOT&);
      template<class GOT> friend
      LocalAccess<GOT, typename Impl::DefaultMemorySpace<GOT>::type, ReadWrite> readWrite (GOT&);
    };
  } // namespace Impl

  template<class GOT>
  Impl::LocalAccess<GOT, typename Impl::DefaultMemorySpace<GOT>::type, ReadOnly>
  readOnly (GOT& G)
  {
    std::cout << "readOnly(" << G.name () << ")" << std::endl;
    return {G, Impl::DefaultMemorySpace<GOT>::space (G), true};
  }

  template<class GOT>
  Impl::LocalAccess<GOT, typename Impl::DefaultMemorySpace<GOT>::type, WriteOnly>
  writeOnly (GOT& G)
  {
    std::cout << "writeOnly(" << G.name () << ")" << std::endl;
    return {G, Impl::DefaultMemorySpace<GOT>::space (G), true};
  }

  template<class GOT>
  Impl::LocalAccess<GOT, typename Impl::DefaultMemorySpace<GOT>::type, ReadWrite>
  readWrite (GOT& G)
  {
    std::cout << "readWrite(" << G.name () << ")" << std::endl;  
    return {G, Impl::DefaultMemorySpace<GOT>::space (G), true};
  }
} // namespace Harness

namespace { // (anonymous)

  using Tpetra::TestingUtilities::getDefaultComm;
  using Teuchos::outArg;
  using Teuchos::RCP;  
  using Teuchos::rcp;
  using Teuchos::REDUCE_MIN;
  using Teuchos::reduceAll;
  using std::endl;
  using GST = Tpetra::global_size_t;
  using map_type = Tpetra::Map<>;
  using multivec_type = Tpetra::MultiVector<>;
  using vec_type = Tpetra::Vector<>;
  using GO = map_type::global_ordinal_type;

#if 0
  Tpetra::MultiVector<> X (...);

  auto X_lcl = localVectorReadOnly (X).on (Cuda);
  auto Y_lcl = localVectorReadWrite (Y).on (Cuda);

  // Look ma, it's an axpy!
  Kokkos::parallel_for ("My happy kernel", range (0, N),
    KOKKOS_LAMBDA (const int lclRow) {
      Y_lcl(lclRow) += alpha * X_lcl(lclRow);
    });
#endif // 0			

  //
  // UNIT TESTS
  //

  TEUCHOS_UNIT_TEST( MultiVector, Harness )
  {
    constexpr bool debug = true;

    RCP<Teuchos::FancyOStream> outPtr = debug ?
      Teuchos::getFancyOStream (Teuchos::rcpFromRef (std::cerr)) :
      Teuchos::rcpFromRef (out);
    Teuchos::FancyOStream& myOut = *outPtr;

    myOut << "Test: MultiVector, Harness" << endl;
    Teuchos::OSTab tab0 (myOut);

    myOut << "Create a Map" << endl;
    auto comm = getDefaultComm ();
    const auto INVALID = Teuchos::OrdinalTraits<GST>::invalid ();
    const size_t numLocal = 13;
    const size_t numVecs  = 3;
    const GO indexBase = 0;
    auto map = rcp (new map_type (INVALID, numLocal, indexBase, comm));

    myOut << "Create a MultiVector, and make sure that it has "
      "the right number of vectors (columns)" << endl;
    multivec_type mvec (map, numVecs);
    TEST_EQUALITY( mvec.getNumVectors (), numVecs );

    myOut << "Create a Vector, and make sure that "
      "it has exactly one vector (column)" << endl;
    vec_type vec (map);
    TEST_EQUALITY_CONST(vec.getNumVectors (), size_t (1));

    // Make sure that the test passed on all processes, not just Proc 0.
    int lclSuccess = success ? 1 : 0;
    int gblSuccess = 1;
    reduceAll<int, int> (*comm, REDUCE_MIN, lclSuccess, outArg (gblSuccess));
    TEST_ASSERT( gblSuccess == 1 );
  }
} // namespace (anonymous)

int
main (int argc, char* argv[])
{
  Tpetra::ScopeGuard tpetraScope (&argc, &argv);
  const int errCode =
    Teuchos::UnitTestRepository::runUnitTestsFromMain (argc, argv);
  return errCode;
}

