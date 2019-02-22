// @HEADER
//
// ***********************************************************************
//
//        MueLu: A package for multigrid based preconditioning
//                  Copyright 2012 Sandia Corporation
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
// Questions? Contact
//                    Jonathan Hu       (jhu@sandia.gov)
//                    Andrey Prokopenko (aprokop@sandia.gov)
//                    Ray Tuminaro      (rstumin@sandia.gov)
//
// ***********************************************************************
//
// @HEADER
#ifndef MUELU_GEOMETRICINTERPOLATIONPFACTORY_DEF_HPP
#define MUELU_GEOMETRICINTERPOLATIONPFACTORY_DEF_HPP

#include "Xpetra_CrsGraph.hpp"
#include "Xpetra_CrsMatrixUtils.hpp"

#include "MueLu_MasterList.hpp"
#include "MueLu_Monitor.hpp"
#include "MueLu_Aggregates.hpp"

// Including this one last ensure that the short names of the above headers are defined properly
#include "MueLu_GeometricInterpolationPFactory_decl.hpp"

namespace MueLu {

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  RCP<const ParameterList> GeometricInterpolationPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node>::GetValidParameterList() const {
    RCP<ParameterList> validParamList = rcp(new ParameterList());

#define SET_VALID_ENTRY(name) validParamList->setEntry(name, MasterList::getEntry(name))
    SET_VALID_ENTRY("interp: interpolation order");
    SET_VALID_ENTRY("interp: build coarse coordinates");
#undef  SET_VALID_ENTRY

    // general variables needed in GeometricInterpolationPFactory
    validParamList->set<RCP<const FactoryBase> >("A",                       Teuchos::null,
                                                 "Generating factory of the matrix A");
    validParamList->set<RCP<const FactoryBase> >("Aggregates",                   Teuchos::null,
                                                 "Aggregates generated by StructuredAggregationFactory used to construct a piece-constant prolongator.");
    validParamList->set<RCP<const FactoryBase> >("prolongatorGraph",             Teuchos::null,
                                                 "Graph generated by StructuredAggregationFactory used to construct a piece-linear prolongator.");
    validParamList->set<RCP<const FactoryBase> >("Coordinates",                  Teuchos::null,
                                                 "Fine level coordinates used to construct piece-wise linear prolongator and coarse level coordinates.");
    validParamList->set<RCP<const FactoryBase> >("coarseCoordinatesFineMap",     Teuchos::null,
                                                 "map of the coarse coordinates' GIDs as indexed on the fine mesh.");
    validParamList->set<RCP<const FactoryBase> >("coarseCoordinatesMap",         Teuchos::null,
                                                 "map of the coarse coordinates' GIDs as indexed on the coarse mesh.");
    validParamList->set<RCP<const FactoryBase> >("Nullspace",                    Teuchos::null,
                                                 "Fine level nullspace used to construct the coarse level nullspace.");
    validParamList->set<RCP<const FactoryBase> >("numDimensions",                Teuchos::null,
                                                 "Number of spacial dimensions in the problem.");
    validParamList->set<RCP<const FactoryBase> >("lCoarseNodesPerDim",           Teuchos::null,
                                                 "Number of nodes per spatial dimension on the coarse grid.");

    return validParamList;
  }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  void GeometricInterpolationPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node>::
  DeclareInput(Level& fineLevel, Level& coarseLevel) const {
    const ParameterList& pL = GetParameterList();

    Input(fineLevel, "A");
    Input(fineLevel, "Nullspace");
    Input(fineLevel, "numDimensions");
    Input(fineLevel, "prolongatorGraph");
    Input(fineLevel, "lCoarseNodesPerDim");

    if( pL.get<bool>("interp: build coarse coordinates") ||
        (pL.get<int>("interp: interpolation order") == 1) ) {
      Input(fineLevel, "Coordinates");
      Input(fineLevel, "coarseCoordinatesFineMap");
      Input(fineLevel, "coarseCoordinatesMap");
    }

  }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  void GeometricInterpolationPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node>::
  Build(Level& fineLevel, Level &coarseLevel) const {
    return BuildP(fineLevel, coarseLevel);
  }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  void GeometricInterpolationPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node>::
  BuildP(Level &fineLevel, Level &coarseLevel) const {
    FactoryMonitor m(*this, "BuildP", coarseLevel);

    // Set debug outputs based on environment variable
    RCP<Teuchos::FancyOStream> out;
    if(const char* dbg = std::getenv("MUELU_GEOMETRICINTERPOLATIONPFACTORY_DEBUG")) {
      out = Teuchos::fancyOStream(Teuchos::rcpFromRef(std::cout));
      out->setShowAllFrontMatter(false).setShowProcRank(true);
    } else {
      out = Teuchos::getFancyOStream(rcp(new Teuchos::oblackholestream()));
    }

    *out << "Starting GeometricInterpolationPFactory::BuildP." << std::endl;

    // Get inputs from the parameter list
    const ParameterList& pL = GetParameterList();
    const bool buildCoarseCoordinates = pL.get<bool>("interp: build coarse coordinates");
    const int interpolationOrder      = pL.get<int> ("interp: interpolation order");
    const int numDimensions           = Get<int>(fineLevel, "numDimensions");

    // Declared main input/outputs to be retrieved and placed on the fine resp. coarse level
    RCP<Matrix> A = Get<RCP<Matrix> >(fineLevel, "A");
    RCP<const CrsGraph> prolongatorGraph = Get<RCP<CrsGraph> >(fineLevel, "prolongatorGraph");
    RCP<realvaluedmultivector_type> fineCoordinates, coarseCoordinates;
    RCP<Matrix> P;

    // Check if we need to build coarse coordinates as they are used if we construct
    // a linear interpolation prolongator
    if(buildCoarseCoordinates || (interpolationOrder == 1)) {
      SubFactoryMonitor sfm(*this, "BuildCoordinates", coarseLevel);
      RCP<const Map> coarseCoordsFineMap = Get< RCP<const Map> >(fineLevel, "coarseCoordinatesFineMap");
      RCP<const Map> coarseCoordsMap = Get< RCP<const Map> >(fineLevel, "coarseCoordinatesMap");
      fineCoordinates   = Get< RCP<realvaluedmultivector_type> >(fineLevel, "Coordinates");
      coarseCoordinates = Xpetra::MultiVectorFactory<real_type,LO,GO,Node>::Build(coarseCoordsFineMap,
                                                                                  fineCoordinates->getNumVectors());
      RCP<const Import> coordsImporter = ImportFactory::Build(fineCoordinates->getMap(),
                                                              coarseCoordsFineMap);
      coarseCoordinates->doImport(*fineCoordinates, *coordsImporter, Xpetra::INSERT);
      coarseCoordinates->replaceMap(coarseCoordsMap);

      Set(coarseLevel, "Coordinates", coarseCoordinates);
    }

    *out << "Fine and coarse coordinates have been loaded from the fine level and set on the coarse level." << std::endl;

    if(interpolationOrder == 0) {
      SubFactoryMonitor sfm(*this, "BuildConstantP", coarseLevel);
      // Compute the prolongator using piece-wise constant interpolation
      BuildConstantP(P, prolongatorGraph, A);
    } else if(interpolationOrder == 1) {
      // Compute the prolongator using piece-wise linear interpolation
      // First get all the required coordinates to compute the local part of P
      RCP<realvaluedmultivector_type> ghostCoordinates
        = Xpetra::MultiVectorFactory<real_type,LO,GO,NO>::Build(prolongatorGraph->getColMap(),
                                                                fineCoordinates->getNumVectors());
      RCP<const Import> ghostImporter = ImportFactory::Build(coarseCoordinates->getMap(),
                                                             prolongatorGraph->getColMap());
      ghostCoordinates->doImport(*coarseCoordinates, *ghostImporter, Xpetra::INSERT);

      SubFactoryMonitor sfm(*this, "BuildLinearP", coarseLevel);
      BuildLinearP(A, prolongatorGraph, fineCoordinates, ghostCoordinates, numDimensions, P);
    }

    *out << "The prolongator matrix has been built." << std::endl;

    {
      SubFactoryMonitor sfm(*this, "BuildNullspace", coarseLevel);
      // Build the coarse nullspace
      RCP<MultiVector> fineNullspace   = Get< RCP<MultiVector> > (fineLevel, "Nullspace");
      RCP<MultiVector> coarseNullspace = MultiVectorFactory::Build(P->getDomainMap(),
                                                                   fineNullspace->getNumVectors());
      P->apply(*fineNullspace, *coarseNullspace, Teuchos::TRANS, Teuchos::ScalarTraits<SC>::one(),
               Teuchos::ScalarTraits<SC>::zero());
      Set(coarseLevel, "Nullspace", coarseNullspace);
    }

    *out << "The coarse nullspace is constructed and set on the coarse level." << std::endl;

    Array<LO> lNodesPerDir = Get<Array<LO> >(fineLevel, "lCoarseNodesPerDim");
    Set(coarseLevel, "numDimensions", numDimensions);
    Set(coarseLevel, "lNodesPerDim", lNodesPerDir);
    Set(coarseLevel, "P", P);

    *out << "GeometricInterpolationPFactory::BuildP has completed." << std::endl;

  } // BuildP

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  void GeometricInterpolationPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node>::
  BuildConstantP(RCP<Matrix>& P, RCP<const CrsGraph>& prolongatorGraph, RCP<Matrix>& A) const {

    // Set debug outputs based on environment variable
    RCP<Teuchos::FancyOStream> out;
    if(const char* dbg = std::getenv("MUELU_GEOMETRICINTERPOLATIONPFACTORY_DEBUG")) {
      out = Teuchos::fancyOStream(Teuchos::rcpFromRef(std::cout));
      out->setShowAllFrontMatter(false).setShowProcRank(true);
    } else {
      out = Teuchos::getFancyOStream(rcp(new Teuchos::oblackholestream()));
    }

    *out << "BuildConstantP" << std::endl;

    std::vector<size_t> strideInfo(1);
    strideInfo[0]    = A->GetFixedBlockSize();
    RCP<const StridedMap> stridedDomainMap =
      StridedMapFactory::Build(prolongatorGraph->getDomainMap(), strideInfo);

    *out << "Call prolongator constructor" << std::endl;

    // Create the prolongator matrix and its associated objects
    RCP<ParameterList> dummyList = rcp(new ParameterList());
    P = rcp(new CrsMatrixWrap(prolongatorGraph, dummyList));
    RCP<CrsMatrix> PCrs = rcp_dynamic_cast<CrsMatrixWrap>(P)->getCrsMatrix();
    PCrs->setAllToScalar(1.0);
    PCrs->fillComplete();

    // set StridingInformation of P
    if (A->IsView("stridedMaps") == true) {
      P->CreateView("stridedMaps", A->getRowMap("stridedMaps"), stridedDomainMap);
    } else {
      P->CreateView("stridedMaps", P->getRangeMap(), stridedDomainMap);
    }

  } // BuildConstantP

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  void GeometricInterpolationPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node>::
  BuildLinearP(RCP<Matrix>& A, RCP<const CrsGraph>& prolongatorGraph,
               RCP<realvaluedmultivector_type>& fineCoordinates,
               RCP<realvaluedmultivector_type>& ghostCoordinates,
               const int numDimensions, RCP<Matrix>& P) const {

    // Set debug outputs based on environment variable
    RCP<Teuchos::FancyOStream> out;
    if(const char* dbg = std::getenv("MUELU_GEOMETRICINTERPOLATIONPFACTORY_DEBUG")) {
      out = Teuchos::fancyOStream(Teuchos::rcpFromRef(std::cout));
      out->setShowAllFrontMatter(false).setShowProcRank(true);
    } else {
      out = Teuchos::getFancyOStream(rcp(new Teuchos::oblackholestream()));
    }

    *out << "Entering BuildLinearP" << std::endl;

    // Extract coordinates for interpolation stencil calculations
    const LO numFineNodes  = fineCoordinates->getLocalLength();
    const LO numGhostNodes = ghostCoordinates->getLocalLength();
    Array<ArrayRCP<const real_type> > fineCoords(3);
    Array<ArrayRCP<const real_type> > ghostCoords(3);
    const real_type realZero = Teuchos::as<real_type>(0.0);
    ArrayRCP<real_type> fineZero(numFineNodes, realZero);
    ArrayRCP<real_type> ghostZero(numGhostNodes, realZero);
    for(int dim = 0; dim < 3; ++dim) {
      if(dim < numDimensions) {
        fineCoords[dim]  = fineCoordinates->getData(dim);
        ghostCoords[dim] = ghostCoordinates->getData(dim);
      } else {
        fineCoords[dim]  = fineZero;
        ghostCoords[dim] = ghostZero;
      }
    }

    *out << "Coordinates extracted from the multivectors!" << std::endl;

    // Compute 2^numDimensions using bit logic to avoid round-off errors
    const int numInterpolationPoints = 1 << numDimensions;
    const int dofsPerNode = A->GetFixedBlockSize();

    std::vector<size_t> strideInfo(1);
    strideInfo[0] = dofsPerNode;
    RCP<const StridedMap> stridedDomainMap =
      StridedMapFactory::Build(prolongatorGraph->getDomainMap(), strideInfo);

    *out << "The maps of P have been computed" << std::endl;

    RCP<ParameterList> dummyList = rcp(new ParameterList());
    P = rcp(new CrsMatrixWrap(prolongatorGraph, dummyList));
    RCP<CrsMatrix> PCrs = rcp_dynamic_cast<CrsMatrixWrap>(P)->getCrsMatrix();
    PCrs->resumeFill(); // The Epetra matrix is considered filled at this point.

    LO interpolationNodeIdx = 0, rowIdx = 0;
    ArrayView<const LO> colIndices;
    Array<SC> values;
    Array<Array<real_type> > coords(numInterpolationPoints + 1);
    Array<real_type> stencil(numInterpolationPoints);
    for(LO nodeIdx = 0; nodeIdx < numFineNodes; ++nodeIdx) {
      if(PCrs->getNumEntriesInLocalRow(nodeIdx*dofsPerNode) == 1) {
        values.resize(1);
        values[0] = 1.0;
        for(LO dof = 0; dof < dofsPerNode; ++dof) {
          rowIdx = nodeIdx*dofsPerNode + dof;
          prolongatorGraph->getLocalRowView(rowIdx, colIndices);
          PCrs->replaceLocalValues(rowIdx, colIndices, values());
        }
      } else {
        // Extract the coordinates associated with the current node
        // and the neighboring coarse nodes
        coords[0].resize(3);
        for(int dim = 0; dim < 3; ++dim) {
          coords[0][dim] = fineCoords[dim][nodeIdx];
        }
        prolongatorGraph->getLocalRowView(nodeIdx*dofsPerNode, colIndices);
        for(int interpolationIdx=0; interpolationIdx < numInterpolationPoints; ++interpolationIdx) {
          coords[interpolationIdx + 1].resize(3);
          interpolationNodeIdx = colIndices[interpolationIdx] / dofsPerNode;
          for(int dim = 0; dim < 3; ++dim) {
            coords[interpolationIdx + 1][dim] = ghostCoords[dim][interpolationNodeIdx];
          }
        }
        ComputeLinearInterpolationStencil(numDimensions, numInterpolationPoints, coords, stencil);
        values.resize(numInterpolationPoints);
        for(LO valueIdx = 0; valueIdx < numInterpolationPoints; ++valueIdx) {
          values[valueIdx] = Teuchos::as<SC>(stencil[valueIdx]);
        }

        // Set values in all the rows corresponding to nodeIdx
        for(LO dof = 0; dof < dofsPerNode; ++dof) {
          rowIdx = nodeIdx*dofsPerNode + dof;
          prolongatorGraph->getLocalRowView(rowIdx, colIndices);
          PCrs->replaceLocalValues(rowIdx, colIndices, values());
        }
      }
    }

    *out << "The calculation of the interpolation stencils has completed." << std::endl;

    PCrs->fillComplete();

    *out << "All values in P have been set and expertStaticFillComplete has been performed." << std::endl;

    // set StridingInformation of P
    if (A->IsView("stridedMaps") == true) {
      P->CreateView("stridedMaps", A->getRowMap("stridedMaps"), stridedDomainMap);
    } else {
      P->CreateView("stridedMaps", P->getRangeMap(), stridedDomainMap);
    }

  } // BuildLinearP


  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  void GeometricInterpolationPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node>::
  ComputeLinearInterpolationStencil(const int numDimensions, const int numInterpolationPoints,
                                    const Array<Array<real_type> > coord,
                                    Array<real_type>& stencil) const {

    //                7         8                Find xi, eta and zeta such that
    //                x---------x
    //               /|        /|          Rx = x_p - sum N_i(xi,eta,zeta)x_i = 0
    //             5/ |      6/ |          Ry = y_p - sum N_i(xi,eta,zeta)y_i = 0
    //             x---------x  |          Rz = z_p - sum N_i(xi,eta,zeta)z_i = 0
    //             |  | *P   |  |
    //             |  x------|--x          We can do this with a Newton solver:
    //             | /3      | /4          We will start with initial guess (xi,eta,zeta) = (0,0,0)
    //             |/        |/            We compute the Jacobian and iterate until convergence...
    //  z  y       x---------x
    //  | /        1         2             Once we have (xi,eta,zeta), we can evaluate all N_i which
    //  |/                                 give us the weights for the interpolation stencil!
    //  o---x
    //

    Teuchos::SerialDenseMatrix<LO,real_type> Jacobian(numDimensions, numDimensions);
    Teuchos::SerialDenseVector<LO,real_type> residual(numDimensions);
    Teuchos::SerialDenseVector<LO,real_type> solutionDirection(numDimensions);
    Teuchos::SerialDenseVector<LO,real_type> paramCoords(numDimensions);
    Teuchos::SerialDenseSolver<LO,real_type> problem;
    int iter = 0, max_iter = 5;
    real_type functions[4][8], norm_ref = 1.0, norm2 = 1.0, tol = 1.0e-5;
    paramCoords.size(numDimensions);

    while( (iter < max_iter) && (norm2 > tol*norm_ref) ) {
      ++iter;
      norm2 = 0.0;
      solutionDirection.size(numDimensions);
      residual.size(numDimensions);
      Jacobian = 0.0;

      // Compute Jacobian and Residual
      GetInterpolationFunctions(numDimensions, paramCoords, functions);
      for(LO i = 0; i < numDimensions; ++i) {
        residual(i) = coord[0][i];                 // Add coordinates from point of interest
        for(LO k = 0; k < numInterpolationPoints; ++k) {
          residual(i) -= functions[0][k]*coord[k+1][i]; //Remove contribution from all coarse points
        }
        if(iter == 1) {
          norm_ref += residual(i)*residual(i);
          if(i == numDimensions - 1) {
            norm_ref = std::sqrt(norm_ref);
          }
        }

        for(LO j = 0; j < numDimensions; ++j) {
          for(LO k = 0; k < numInterpolationPoints; ++k) {
            Jacobian(i,j) += functions[j+1][k]*coord[k+1][i];
          }
        }
      }

      // Set Jacobian, Vectors and solve problem
      problem.setMatrix(Teuchos::rcp(&Jacobian, false));
      problem.setVectors(Teuchos::rcp(&solutionDirection, false), Teuchos::rcp(&residual, false));
      if(problem.shouldEquilibrate()) {problem.factorWithEquilibration(true);}
      problem.solve();

      for(LO i = 0; i < numDimensions; ++i) {
        paramCoords(i) = paramCoords(i) + solutionDirection(i);
      }

      // Recompute Residual norm
      GetInterpolationFunctions(numDimensions, paramCoords, functions);
      for(LO i = 0; i < numDimensions; ++i) {
        real_type tmp = coord[0][i];
        for(LO k = 0; k < numInterpolationPoints; ++k) {
          tmp -= functions[0][k]*coord[k+1][i];
        }
        norm2 += tmp*tmp;
        tmp = 0.0;
      }
      norm2 = std::sqrt(norm2);
    }

    // Load the interpolation values onto the stencil.
    for(LO i = 0; i < numInterpolationPoints; ++i) {
      stencil[i] = functions[0][i];
    }

  } // End ComputeLinearInterpolationStencil

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  void GeometricInterpolationPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node>::
  GetInterpolationFunctions(const LO numDimensions,
                            const Teuchos::SerialDenseVector<LO, real_type> parametricCoordinates,
                            real_type functions[4][8]) const {
    real_type xi = 0.0, eta = 0.0, zeta = 0.0, denominator = 0.0;
    if(numDimensions == 1) {
      xi = parametricCoordinates[0];
      denominator = 2.0;
    } else if(numDimensions == 2) {
      xi  = parametricCoordinates[0];
      eta = parametricCoordinates[1];
      denominator = 4.0;
    } else if(numDimensions == 3) {
      xi   = parametricCoordinates[0];
      eta  = parametricCoordinates[1];
      zeta = parametricCoordinates[2];
      denominator = 8.0;
    }

    functions[0][0] = (1.0 - xi)*(1.0 - eta)*(1.0 - zeta) / denominator;
    functions[0][1] = (1.0 + xi)*(1.0 - eta)*(1.0 - zeta) / denominator;
    functions[0][2] = (1.0 - xi)*(1.0 + eta)*(1.0 - zeta) / denominator;
    functions[0][3] = (1.0 + xi)*(1.0 + eta)*(1.0 - zeta) / denominator;
    functions[0][4] = (1.0 - xi)*(1.0 - eta)*(1.0 + zeta) / denominator;
    functions[0][5] = (1.0 + xi)*(1.0 - eta)*(1.0 + zeta) / denominator;
    functions[0][6] = (1.0 - xi)*(1.0 + eta)*(1.0 + zeta) / denominator;
    functions[0][7] = (1.0 + xi)*(1.0 + eta)*(1.0 + zeta) / denominator;

    functions[1][0] = -(1.0 - eta)*(1.0 - zeta) / denominator;
    functions[1][1] =  (1.0 - eta)*(1.0 - zeta) / denominator;
    functions[1][2] = -(1.0 + eta)*(1.0 - zeta) / denominator;
    functions[1][3] =  (1.0 + eta)*(1.0 - zeta) / denominator;
    functions[1][4] = -(1.0 - eta)*(1.0 + zeta) / denominator;
    functions[1][5] =  (1.0 - eta)*(1.0 + zeta) / denominator;
    functions[1][6] = -(1.0 + eta)*(1.0 + zeta) / denominator;
    functions[1][7] =  (1.0 + eta)*(1.0 + zeta) / denominator;

    functions[2][0] = -(1.0 - xi)*(1.0 - zeta) / denominator;
    functions[2][1] = -(1.0 + xi)*(1.0 - zeta) / denominator;
    functions[2][2] =  (1.0 - xi)*(1.0 - zeta) / denominator;
    functions[2][3] =  (1.0 + xi)*(1.0 - zeta) / denominator;
    functions[2][4] = -(1.0 - xi)*(1.0 + zeta) / denominator;
    functions[2][5] = -(1.0 + xi)*(1.0 + zeta) / denominator;
    functions[2][6] =  (1.0 - xi)*(1.0 + zeta) / denominator;
    functions[2][7] =  (1.0 + xi)*(1.0 + zeta) / denominator;

    functions[3][0] = -(1.0 - xi)*(1.0 - eta) / denominator;
    functions[3][1] = -(1.0 + xi)*(1.0 - eta) / denominator;
    functions[3][2] = -(1.0 - xi)*(1.0 + eta) / denominator;
    functions[3][3] = -(1.0 + xi)*(1.0 + eta) / denominator;
    functions[3][4] =  (1.0 - xi)*(1.0 - eta) / denominator;
    functions[3][5] =  (1.0 + xi)*(1.0 - eta) / denominator;
    functions[3][6] =  (1.0 - xi)*(1.0 + eta) / denominator;
    functions[3][7] =  (1.0 + xi)*(1.0 + eta) / denominator;

  } // End GetInterpolationFunctions

} // namespace MueLu

#endif // MUELU_GEOMETRICINTERPOLATIONPFACTORY_DEF_HPP
