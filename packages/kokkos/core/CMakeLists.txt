

KOKKOS_SUBPACKAGE(Core)

ADD_SUBDIRECTORY(src)

KOKKOS_ADD_TEST_DIRECTORIES(unit_test)
KOKKOS_ADD_TEST_DIRECTORIES(perf_test)

KOKKOS_SUBPACKAGE_POSTPROCESS()


