#include <Kokkos_Core.hpp>

#include <Kokkos_Threads.hpp>
#include <Threads/Kokkos_Threads_TaskPolicy.hpp>

#include "Teuchos_CommandLineProcessor.hpp"

using namespace std;

typedef double value_type;
typedef int    ordinal_type;
typedef int    size_type;

typedef Kokkos::Threads exec_space;

#include "example_ichol_unblocked.hpp"

using namespace Example;

int main (int argc, char *argv[]) {

  Teuchos::CommandLineProcessor clp;
  clp.setDocString("This example program demonstrates ICholUnblocked algorithm on Kokkos::Threads execution space.\n");

  int nthreads = 1;
  clp.setOption("nthreads", &nthreads, "Number of threads");

  int numa = 0;
  clp.setOption("numa", &numa, "Number of numa node");

  int core_per_numa = 0;
  clp.setOption("core-per-numa", &core_per_numa, "Number of cores per numa node");

  int max_task_dependence = 10;
  clp.setOption("max-task-dependence", &max_task_dependence, "Max number of task dependence");

  int team_size = 1;
  clp.setOption("team-size", &team_size, "Team size");

  bool verbose = false;
  clp.setOption("enable-verbose", "disable-verbose", &verbose, "Flag for verbose printing");

  string file_input = "test.mtx";
  clp.setOption("file-input", &file_input, "Input file (MatrixMarket SPD matrix)");

  string algorithm = "UnblockedOpt1";
  clp.setOption("algorithm-variant", &algorithm, "Algorithm variant (Dummy, UnblockedOpt1, UnblockedOpt2)");

  clp.recogniseAllOptions(true);
  clp.throwExceptions(false);

  Teuchos::CommandLineProcessor::EParseCommandLineReturn r_parse= clp.parse( argc, argv );

  if (r_parse == Teuchos::CommandLineProcessor::PARSE_HELP_PRINTED) return 0;
  if (r_parse != Teuchos::CommandLineProcessor::PARSE_SUCCESSFUL  ) return -1;
  
  int r_val = 0;
  {
    exec_space::initialize(nthreads, numa, core_per_numa);
    exec_space::print_configuration(cout, true);
    
    int variant = 0;
    if      (algorithm == "UnblockedOpt1")
      variant = AlgoIChol::UnblockedOpt1;
    else if (algorithm == "UnblockedOpt2")
      variant = AlgoIChol::UnblockedOpt2;
    else if (algorithm == "Dummy")
      variant = AlgoIChol::Dummy;
    else      
      ERROR(">> Not supported algorithm variant");
    
    r_val = exampleICholUnblocked
      <value_type,ordinal_type,size_type,exec_space,void>
      (file_input, max_task_dependence, team_size, variant, verbose);
    
    exec_space::finalize();
  }

  return r_val;
}
