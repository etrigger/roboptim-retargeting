// Copyright (C) 2013 by Thomas Moulard, AIST, CNRS, INRIA.
//
// This file is part of the roboptim.
//
// roboptim is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// roboptim is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with roboptim.  If not, see <http://www.gnu.org/licenses/>.


#define BOOST_TEST_MODULE choreonoid_body_trajectory

#include <boost/test/unit_test.hpp>
#include <boost/test/output_test_stream.hpp>

#include <roboptim/core/visualization/gnuplot.hh>
#include <roboptim/core/visualization/gnuplot-commands.hh>
#include <roboptim/core/visualization/gnuplot-function.hh>

#include "roboptim/retargeting/function/joint-to-marker/choreonoid.hh"

#include <cnoid/BodyLoader>
#include <cnoid/BodyMotion>

#include "tests-config.h"

using namespace roboptim;
using namespace roboptim::visualization;
using namespace roboptim::retargeting;

using boost::test_tools::output_test_stream;

//FIXME: we should embed the copy.
std::string modelFilePath
("/home/moulard/HRP4C-release/HRP4Cg2.yaml");

BOOST_AUTO_TEST_CASE (simple)
{
  // Configure log4cxx
  configureLog4cxx ();

  //FIXME: we should embed the copy.
  std::string modelFilePath
    ("/home/moulard/HRP4C-release/HRP4Cg2.yaml");

  // Loading robot.
  cnoid::BodyLoader loader;
  cnoid::BodyPtr robot = loader.load (modelFilePath);
  if (!robot)
    throw std::runtime_error ("failed to load model");

  // Loading the motion.
  cnoid::BodyMotionPtr bodyMotion = boost::make_shared<cnoid::BodyMotion> ();

  //FIXME: we should embed the copy.
  bodyMotion->loadStandardYAMLformat ("/home/moulard/29_07-hrp4c-initial-short.yaml");

  // Body Interaction Mesh
  cnoid::BodyIMeshPtr mesh = boost::make_shared<cnoid::BodyIMesh> ();
  if (!mesh->addBody (robot, bodyMotion))
    throw std::runtime_error ("failed to add body to body interaction mesh");
  if (!mesh->initialize ())
        throw std::runtime_error ("failed to initialize body interaction mesh");

  Function::vector_t x
    (6 + mesh->bodyInfo (0).body->numJoints ());
  x.setZero ();

  boost::shared_ptr<JointToMarkerPositionChoreonoid<
    EigenMatrixDense> >
    jointToMarker =
    boost::make_shared<JointToMarkerPositionChoreonoid<
      EigenMatrixDense> > (mesh, 0);

  std::cout << (*jointToMarker) (x) << std::endl;
}
