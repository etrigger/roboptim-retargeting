#include <boost/make_shared.hpp>

#include <roboptim/retargeting/function/minimum-jerk-trajectory.hh>
#include <roboptim/retargeting/function/forward-geometry/choreonoid.hh>
#include <roboptim/trajectory/vector-interpolation.hh>

#include <cnoid/BodyLoader>

#define BOOST_TEST_MODULE forward_geometry_chorenoid

#include <boost/test/unit_test.hpp>
#include <boost/test/output_test_stream.hpp>

#include "tests-config.h"

using boost::test_tools::output_test_stream;

using namespace roboptim;
using namespace roboptim::retargeting;

//FIXME: we should embed the copy.
std::string modelFilePath
("/home/moulard/HRP4C-release/HRP4Cg2.yaml");

BOOST_AUTO_TEST_CASE (root_link)
{
  configureLog4cxx ();

  // Loading robot.
  cnoid::BodyLoader loader;
  cnoid::BodyPtr robot = loader.load (modelFilePath);
  if (!robot)
    throw std::runtime_error ("failed to load model");

  typedef ForwardGeometryChoreonoid<EigenMatrixDense>::jacobian_t jacobian_t;
  typedef ForwardGeometryChoreonoid<EigenMatrixDense>::vector_t vector_t;
  ForwardGeometryChoreonoid<EigenMatrixDense> forwardGeometry (robot, 0);

  vector_t x (6 + robot->numJoints ());
  vector_t res (3);

  // Zero
  x.setZero ();
  res = forwardGeometry (x);
  std::cout
    << "X:" << incindent << iendl
    << x << decindent << iendl
    << "ForwardGeometry(X): " << incindent << iendl
    << res << decindent << iendl;

  BOOST_CHECK_EQUAL (res[0], 0.);
  BOOST_CHECK_EQUAL (res[1], 0.);
  BOOST_CHECK_EQUAL (res[2], 0.);

  // 1, 2, 3, Zero*N
  x.setZero ();
  x[0] = 1.;
  x[1] = 4.;
  x[2] = 8.;
  res = forwardGeometry (x);
  std::cout
    << "X:" << incindent << iendl
    << x << decindent << iendl
    << "ForwardGeometry(X): " << incindent << iendl
    << res << decindent << iendl;

  BOOST_CHECK_EQUAL (res[0], 1.);
  BOOST_CHECK_EQUAL (res[1], 4.);
  BOOST_CHECK_EQUAL (res[2], 8.);

  // Jacobian is identity.
  x.setRandom (x.size ());
  std::cout
    << "X:" << incindent << iendl
    << x << decindent << iendl;
  jacobian_t jacobian = forwardGeometry.jacobian (x);
  std::cout
    << "ForwardGeometry.jacobian(X): " << incindent << iendl
    << jacobian << decindent << iendl;

  for (std::size_t i = 0; i < 3; ++i)
    BOOST_CHECK_EQUAL (jacobian (i, i), 1.);
}