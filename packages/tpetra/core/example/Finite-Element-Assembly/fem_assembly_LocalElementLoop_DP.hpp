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
#ifndef TPETRAEXAMPLES_FEM_ASSEMBLY_LOCALELEMENTLOOP_DP_HPP
#define TPETRAEXAMPLES_FEM_ASSEMBLY_LOCALELEMENTLOOP_DP_HPP

#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>

#include <Tpetra_Core.hpp>
#include <Tpetra_Version.hpp>
#include <MatrixMarket_Tpetra.hpp>
#include <Teuchos_RCP.hpp>
#include <Teuchos_FancyOStream.hpp>

#include "fem_assembly_typedefs.hpp"
#include "fem_assembly_MeshDatabase.hpp"
#include "fem_assembly_Element.hpp"
#include "fem_assembly_utility.hpp"
#include "fem_assembly_commandLineOpts.hpp"

namespace TpetraExamples
{
using comm_ptr_t = Teuchos::RCP<const Teuchos::Comm<int> >;

int executeLocalElementLoopDP_(const comm_ptr_t& comm, const struct CmdLineOpts& opts);



int executeLocalElementLoopDP(const comm_ptr_t& comm, const struct CmdLineOpts & opts)
{
  using Teuchos::RCP;

  // The output stream 'out' will ignore any output not from Process 0.
  RCP<Teuchos::FancyOStream> pOut = getOutputStream(*comm);
  Teuchos::FancyOStream& out = *pOut;

  out << "================================================================================" << std::endl
      << "=  Local Element Loop (Dynamic Profile)"    << std::endl
      << "================================================================================" << std::endl
      << std::endl;

  int status = 0;
  for(size_t i=0; i<opts.repetitions; ++i)
  {
    status += executeLocalElementLoopDP_(comm, opts);
  }
  return status;
}



int executeLocalElementLoopDP_(const comm_ptr_t& comm, const struct CmdLineOpts& opts)
{
  using Teuchos::RCP;
  using Teuchos::TimeMonitor;

  const global_ordinal_t GO_INVALID = Teuchos::OrdinalTraits<global_ordinal_t>::invalid();

  // The output stream 'out' will ignore any output not from Process 0.
  RCP<Teuchos::FancyOStream> pOut = getOutputStream(*comm);
  Teuchos::FancyOStream& out = *pOut;

  // Processor decomp (only works on perfect squares)
  int numProcs  = comm->getSize();
  int sqrtProcs = sqrt(numProcs);

  if(sqrtProcs*sqrtProcs != numProcs)
  {
    if(0 == comm->getRank())
      std::cerr << "Error: Invalid number of processors provided, num processors must be a perfect square." << std::endl;
    return -1;
  }
  int procx = sqrtProcs;
  int procy = sqrtProcs;

  // Generate a simple 2D mesh
  int nex = opts.numElementsX;
  int ney = opts.numElementsY;

  MeshDatabase mesh(comm,nex,ney,procx,procy);

  if(opts.verbose) mesh.print(std::cout);

  // Build Tpetra Maps
  // -----------------
  // - Doxygen: https://trilinos.org/docs/dev/packages/tpetra/doc/html/classTpetra_1_1Map.html#a24490b938e94f8d4f31b6c0e4fc0ff77
  RCP<const map_t> owned_row_map       = rcp(new map_t(GO_INVALID, mesh.getOwnedNodeGlobalIDs(), 0, comm));
  RCP<const map_t> overlapping_row_map = rcp(new map_t(GO_INVALID, mesh.getGhostNodeGlobalIDs(), 0, comm));
  export_t exporter(overlapping_row_map, owned_row_map);

  if(opts.verbose)
  {
    owned_row_map->describe(out);
    overlapping_row_map->describe(out);
  }

  // Graph Construction
  // ------------------
  auto domain_map = owned_row_map;
  auto range_map  = owned_row_map;

  auto owned_element_to_node_ids = mesh.getOwnedElementToNode();

  RCP<TimeMonitor> timerGlobal = rcp(new TimeMonitor(*TimeMonitor::getNewTimer("X) Global")));
  RCP<TimeMonitor> timerElementLoopGraph = rcp(new TimeMonitor(*TimeMonitor::getNewTimer("1) ElementLoop  (All Graph)")));

  // Type-2 Assembly distinguishes owned and overlapping nodes.
  // - Owned nodes are nodes that only touch elements owned by the same process.
  // - Overlapping nodes are nodes which touch elements owned by a different process.
  //
  // In Type-2 assembly, the graph construction loop looks similar to Type-1 but
  // in this case we insert rows into crs_graph_overlapping, then we fillComplete
  // the overlapping graph.  Next we export contributions from overlapping graph
  // to the owned graph and call fillComplete on the owned graph.
  //
  RCP<graph_t> crs_graph_owned = rcp(new graph_t(owned_row_map, 0));
  RCP<graph_t> crs_graph_overlapping = rcp(new graph_t(overlapping_row_map, 0));

  // Note: Using 4 because we're using quads for this example, so there will be 4 nodes associated with each element.
  Teuchos::Array<global_ordinal_t> global_ids_in_row(4);

  // for each element in the mesh...
  for(size_t element_gidx=0; element_gidx<mesh.getNumOwnedElements(); element_gidx++)
  {
    // Populate global_ids_in_row:
    // - Copy the global node ids for current element into an array.
    // - Since each element's contribution is a clique, we can re-use this for
    //   each row associated with this element's contribution.
    for(size_t element_node_idx=0; element_node_idx<owned_element_to_node_ids.extent(1); element_node_idx++)
    {
      global_ids_in_row[element_node_idx] = owned_element_to_node_ids(element_gidx, element_node_idx);
    }

    // Add the contributions from the current row into the overlapping graph.
    // - For example, if Element 0 contains nodes [0,1,4,5] then we insert the nodes:
    //   - node 0 inserts [0, 1, 4, 5]
    //   - node 1 inserts [0, 1, 4, 5]
    //   - node 4 inserts [0, 1, 4, 5]
    //   - node 5 inserts [0, 1, 4, 5]
    //
    for(size_t element_node_idx=0; element_node_idx<owned_element_to_node_ids.extent(1); element_node_idx++)
    {
      if( mesh.nodeIsOwned(element_node_idx) )
      {
        crs_graph_owned->insertGlobalIndices(global_ids_in_row[element_node_idx], global_ids_in_row());
      }
      else
      {
        crs_graph_overlapping->insertGlobalIndices(global_ids_in_row[element_node_idx], global_ids_in_row());
      }
    }
  }
  timerElementLoopGraph = Teuchos::null;

  // Call fillComplete on the crs_graph_owned to 'finalize' it.
  {
    TimeMonitor timer(*TimeMonitor::getNewTimer("2) FillComplete (Overlapping Graph)"));
    crs_graph_overlapping->fillComplete(domain_map, range_map);
  }

  // Need to Export and fillComplete the crs_graph_owned structure...
  // NOTE: Need to implement a graph transferAndFillComplete() method.
  {
    TimeMonitor timer(*TimeMonitor::getNewTimer("3) Export       (Owned Graph)"));
    crs_graph_owned->doExport(*crs_graph_overlapping, exporter, Tpetra::INSERT);
  }

  {
    TimeMonitor timer(*TimeMonitor::getNewTimer("4) FillComplete (Owned Graph)"));
    crs_graph_owned->fillComplete();
  }

  // Let's see what we have
  if(opts.verbose)
  {
    crs_graph_owned->describe(out, Teuchos::VERB_EXTREME);
    crs_graph_overlapping->describe(out, Teuchos::VERB_EXTREME);
  }

  // Matrix Fill
  // -------------------
  // In this example, we're using a simple stencil of values for the matrix fill:
  //
  //    +-----+-----+-----+-----+
  //    |  2  | -1  |     | -1  |
  //    +-----+-----+-----+-----+
  //    | -1  |  2  | -1  |     |
  //    +-----+-----+-----+-----+
  //    |     | -1  |  2  | -1  |
  //    +-----+-----+-----+-----+
  //    | -1  |     | -1  |  2  |
  //    +-----+-----+-----+-----+
  //
  // For Type 2 matrix fill, we create a crs_matrix object for both owned
  // and overlapping rows.  We will only fill the overlapping graph using
  // the same method as we filled the graph but in this case, nodes
  // associated with each element will receive contributions according to
  // the row in this stencil.
  //
  // In this example, the calls to sumIntoGlobalValues() on 1 core will look like:
  //   Element 0
  // - sumIntoGlobalValues( 0,  [  0  1  5  4  ],  [  2  -1  0  -1  ])
  // - sumIntoGlobalValues( 1,  [  0  1  5  4  ],  [  -1  2  -1  0  ])
  // - sumIntoGlobalValues( 5,  [  0  1  5  4  ],  [  0  -1  2  -1  ])
  // - sumIntoGlobalValues( 4,  [  0  1  5  4  ],  [  -1  0  -1  2  ])
  // Element 1
  // - sumIntoGlobalValues( 1,  [  1  2  6  5  ],  [  2  -1  0  -1  ])
  // - sumIntoGlobalValues( 2,  [  1  2  6  5  ],  [  -1  2  -1  0  ])
  // - sumIntoGlobalValues( 6,  [  1  2  6  5  ],  [  0  -1  2  -1  ])
  // - sumIntoGlobalValues( 5,  [  1  2  6  5  ],  [  -1  0  -1  2  ])
  // Element 2
  // - sumIntoGlobalValues( 2,  [  2  3  7  6  ],  [  2  -1  0  -1  ])
  // - sumIntoGlobalValues( 3,  [  2  3  7  6  ],  [  -1  2  -1  0  ])
  // - sumIntoGlobalValues( 7,  [  2  3  7  6  ],  [  0  -1  2  -1  ])
  // - sumIntoGlobalValues( 6,  [  2  3  7  6  ],  [  -1  0  -1  2  ])
  RCP<TimeMonitor> timerElementLoopMatrix = rcp(new TimeMonitor(*TimeMonitor::getNewTimer("5) ElementLoop  (All Matrix)")));

  // Create owned and overlapping CRS Matrices
  RCP<matrix_t> crs_matrix_owned       = rcp(new matrix_t(crs_graph_owned));
  RCP<matrix_t> crs_matrix_overlapping = rcp(new matrix_t(crs_graph_overlapping));
  RCP<multivector_t> rhs_owned         = rcp(new multivector_t(crs_graph_owned->getRowMap(), 1));
  RCP<multivector_t> rhs_overlapping   = rcp(new multivector_t(crs_graph_overlapping->getRowMap(), 1));

  scalar_2d_array_t element_matrix;
  Kokkos::resize(element_matrix, 4, 4);
  Teuchos::Array<Scalar> element_rhs(4);

  Teuchos::Array<global_ordinal_t> column_global_ids(4);     // global column ids list
  Teuchos::Array<Scalar> column_scalar_values(4);            // scalar values for each column

  // Loop over elements
  for(size_t element_gidx=0; element_gidx<mesh.getNumOwnedElements(); element_gidx++)
  {
    // Get the contributions for the current element
    ReferenceQuad4(element_matrix);
    ReferenceQuad4RHS(element_rhs);

    // Fill the global column ids array for this element
    for(size_t element_node_idx=0; element_node_idx<owned_element_to_node_ids.extent(1); element_node_idx++)
    {
      column_global_ids[element_node_idx] = owned_element_to_node_ids(element_gidx, element_node_idx);
    }

    // For each node (row) on the current element:
    // - populate the values array
    // - add the values to the crs_matrix_owned.
    // Note: hardcoded to 4 here because our example uses quads.
    for(size_t element_node_idx=0; element_node_idx<4; element_node_idx++)
    {
      global_ordinal_t global_row_id = owned_element_to_node_ids(element_gidx, element_node_idx);

      for(size_t col_idx=0; col_idx<4; col_idx++)
      {
        column_scalar_values[col_idx] = element_matrix(element_node_idx, col_idx);
      }

      // For Type-2 Assembly, we only sumInot the overlapping crs_matrix.
      if( mesh.nodeIsOwned(element_node_idx) )
      {
        crs_matrix_owned->sumIntoGlobalValues(global_row_id, column_global_ids, column_scalar_values);
        rhs_owned->sumIntoGlobalValue(global_row_id, 0, element_rhs[element_node_idx]);
      }
      else
      {
        crs_matrix_overlapping->sumIntoGlobalValues(global_row_id, column_global_ids, column_scalar_values);
        rhs_overlapping->sumIntoGlobalValue(global_row_id, 0, element_rhs[element_node_idx]);
      }
    }
  }
  timerElementLoopMatrix = Teuchos::null;

  // After contributions are added, we finalize the crs_matrix in
  // the same manner that we did the crs_graph.
  // On Type-2 assembly, we fillComplete the overlapping matrix, then
  // export contributions to the owned matrix using the exporter, then
  // fillComplete the owned matrix.
  {
    TimeMonitor timer(*TimeMonitor::getNewTimer("6) FillComplete (Overlapping Matrix)"));
    crs_matrix_overlapping->fillComplete(domain_map, range_map);
  }

  {
    TimeMonitor timer(*TimeMonitor::getNewTimer("7) Export       (Owned Matrix)"));
    crs_matrix_owned->doExport(*crs_matrix_overlapping, exporter, Tpetra::ADD);
    rhs_owned->doExport(*rhs_overlapping, exporter, Tpetra::ADD);
  }

  {
    TimeMonitor timer(*TimeMonitor::getNewTimer("8) FillComplete (Owned Matrix)"));
    crs_matrix_owned->fillComplete();
  }

  timerGlobal = Teuchos::null;

  // Print out crs_matrix_owned and crs_matrix_overlapping details.
  if(opts.verbose)
  {
    crs_matrix_owned->describe(out, Teuchos::VERB_EXTREME);
    crs_matrix_overlapping->describe(out, Teuchos::VERB_EXTREME);
  }

  // Save crs_matrix as a MatrixMarket file.
  if(opts.saveMM)
  {
    std::ofstream ofs("crsMatrix_LocalElementLoop_DP.out", std::ofstream::out);
    Tpetra::MatrixMarket::Writer<matrix_t>::writeSparse(ofs, crs_matrix_owned);
    ofs.close();
  }

  return 0;
}


} // namespace TpetraExamples


#endif // TPETRAEXAMPLES_FEM_ASSEMBLY_LOCALELEMENTLOOP_DP_HPP
