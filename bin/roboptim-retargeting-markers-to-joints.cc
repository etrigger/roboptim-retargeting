// Copyright (C) 2014 by Thomas Moulard, AIST, CNRS.
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

#include <fstream>
#include <string>

#include <boost/program_options.hpp>

#include <yaml-cpp/yaml.h>

#include <cnoid/Body>
#include <cnoid/BodyLoader>

#include <libmocap/marker-set.hh>
#include <libmocap/marker-set-factory.hh>
#include <libmocap/marker-trajectory.hh>
#include <libmocap/marker-trajectory-factory.hh>

#include <roboptim/core/solver.hh>
#include <roboptim/core/solver-factory.hh>
#include <roboptim/core/result.hh>
#include <roboptim/core/result-with-warnings.hh>
#include <roboptim/core/util.hh>

#include <roboptim/trajectory/trajectory.hh>

#include <roboptim/retargeting/problem/marker-to-joint-problem-builder.hh>

//FIXME: unduplicate this
static void
writeBodyMotion (const std::string& filename,
		 boost::shared_ptr<roboptim::Trajectory<3> > result)
{
  typedef Eigen::Quaternion<
    roboptim::VectorInterpolation::value_type> quaternion_t;

  std::ofstream fout (filename.c_str ());
  if (!fout.good ())
    throw std::runtime_error ("bad stream");

  int numFrames =
    static_cast<int> (result->parameters ().size () / result->outputSize ());
  double dt = result->length () / numFrames;
  int nDofs = static_cast<int> (result->outputSize ());

  YAML::Emitter out;
  out
    << YAML::Comment("Generated by roboptim-retargeting")
    << YAML::BeginMap
    << YAML::Key << "type"
    << YAML::Value << "BodyMotion"
    << YAML::Key << "components"
    << YAML::Value
    << YAML::BeginSeq

    << YAML::BeginMap
    << YAML::Key << "type"
    << YAML::Value << "MultiValueSeq"
    << YAML::Key << "content"
    << YAML::Value << "JointPosition"
    << YAML::Key << "frameRate"
    << YAML::Value << 1. / dt
    << YAML::Key << "numFrames"
    << YAML::Value << numFrames
    << YAML::Key << "numParts"
    << YAML::Value << nDofs - 6
    << YAML::Key << "frames"
    << YAML::Value

    << YAML::BeginSeq;

  for (int frameId = 0; frameId < numFrames; ++frameId)
    {
      roboptim::VectorInterpolation::vector_t
	oneFrame = (*result) (frameId * dt);

      out << YAML::Flow << YAML::BeginSeq;
      for (int dofId = 6; dofId < result->outputSize (); ++dofId)
	out << oneFrame[dofId];
      out << YAML::EndSeq;
    }
  out << YAML::EndSeq;
  out << YAML::EndMap;

  out
    << YAML::BeginMap
    << YAML::Key << "type"
    << YAML::Value << "MultiSE3Seq"
    << YAML::Key << "content"
    << YAML::Value << "LinkPosition"
    << YAML::Key << "frameRate"
    << YAML::Value << 1. / dt
    << YAML::Key << "numFrames"
    << YAML::Value << numFrames
    << YAML::Key << "numParts"
    << YAML::Value << 1
    << YAML::Key << "format"
    << YAML::Value << "XYZQWQXQYQZ"
    << YAML::Key << "frames"
    << YAML::Value

    << YAML::BeginSeq;
  for (int frameId = 0; frameId < numFrames; ++frameId)
    {
      roboptim::VectorInterpolation::vector_t
	oneFrame = (*result) (frameId * dt);

      // The free floating position (7 parameters) is considered as
      // one part.
      out << YAML::Flow << YAML::BeginSeq << YAML::BeginSeq;
      for (int dofId = 0; dofId < 3; ++dofId)
	out << oneFrame[dofId];

      roboptim::VectorInterpolation::value_type
	norm = oneFrame.segment (3, 3).norm ();

      quaternion_t quaternion;
      quaternion.setIdentity ();

      if (norm >= 1e-10)
	quaternion = Eigen::AngleAxisd
	  (norm, oneFrame.segment (3, 3).normalized ());

      out
	<< quaternion.w ()
	<< quaternion.x () << quaternion.y () << quaternion.z ();

      out << YAML::EndSeq << YAML::EndSeq;
    }
  out << YAML::EndSeq;

  out << YAML::EndMap;

  fout << out.c_str ();
}


static bool parseOptions
(roboptim::retargeting::MarkerToJointProblemOptions& options,
 int argc, const char* argv[])
{
  namespace po = boost::program_options;
  po::options_description desc ("Options");
  desc.add_options ()
    ("help,h", "Print help messages")
    ("marker-trajectory,m",
     po::value<std::string>
     (&options.markersTrajectory)->required (),
     "input markers trajectory used during Motion Capture"
     " (trc or any other format supported by libmocap)")
    ("output-file,o",
     po::value<std::string>
     (&options.outputFile)->required (),
     "output marker trajectory (Choreonoid YAML file)")
    ("trajectory-type,t",
     po::value<std::string>
     (&options.trajectoryType)->default_value ("discrete"),
     "Trajectory type (discrete)")

    ("marker-set,s",
     po::value<std::string> (&options.markerSet)->required (),
     "Marker Set used during Motion Capture"
     " (mars or any other format supported by libmocap)")

    ("robot-model,r",
     po::value<std::string> (&options.robotModel)->required (),
     "Robot Model (Choreonoid YAML file)")

    ("constraint,C",
     po::value<std::vector<std::string> > (&options.constraints),
     "Which constraints should be used?")

    ("disable-joint,d",
     po::value<std::vector<std::string> > (&options.disabledJoints),
     "Exclude a joint from the optimization process")

    ("cost,c",
     po::value<std::string> (&options.cost)->default_value ("null"),
     "What cost function should be used?")
    ("plugin,p",
     po::value<std::string> (&options.plugin)->default_value ("cfsqp"),
     "RobOptim plug-in to be used")
    ;

  po::variables_map vm;
  po::store
    (po::command_line_parser (argc, argv)
     .options (desc)
     .run (),
     vm);

  if (vm.count ("help"))
    {
      std::cout << desc << "\n";
      return false;
    }

  po::notify (vm);

  return true;
}


int safeMain (int argc, const char* argv[])
{
  typedef roboptim::retargeting::denseProblem_t problem_t;
  typedef roboptim::Solver<
    roboptim::GenericDifferentiableFunction<roboptim::EigenMatrixDense>,
    boost::mpl::vector<
      roboptim::GenericLinearFunction<roboptim::EigenMatrixDense>,
      roboptim::GenericDifferentiableFunction<roboptim::EigenMatrixDense>
      >
    >
    solver_t;

  roboptim::retargeting::MarkerToJointProblemOptions options;

  if (!parseOptions (options, argc, argv))
    return 0;

  // Build problem.
  roboptim::retargeting::MarkerToJointProblemBuilder<problem_t>
    builder (options);

  boost::shared_ptr<problem_t> problem;
  roboptim::retargeting::MarkerToJointFunctionData data;
  options.frameId = 0;
  builder (problem, data);

  roboptim::Function::vector_t::Index nFrames =
    static_cast<roboptim::Function::vector_t::Index>
    (data.markersTrajectory.numFrames ());

  for (options.frameId = 0; options.frameId < nFrames; ++options.frameId)
    {
      std::cout << "*** Frame " << options.frameId << roboptim::iendl;
      builder (problem, data);

      if (!problem)
	throw std::runtime_error ("failed to build problem");

      roboptim::SolverFactory<solver_t>
	factory (options.plugin, *problem);
      solver_t& solver = factory ();

      // Set solver parameters.
      solver.parameters ()["max-iterations"].value = 1000;

      solver.parameters ()["ipopt.output_file"].value =
	"/tmp/ipopt.log";
      solver.parameters ()["ipopt.print_level"].value = 5;
      solver.parameters ()["ipopt.expect_infeasible_problem"].value = "no";
      solver.parameters ()["ipopt.nlp_scaling_method"].value = "none";
      solver.parameters ()["ipopt.tol"].value = 1e-3;
      solver.parameters ()["ipopt.dual_inf_tol"].value = 1.;
      solver.parameters ()["ipopt.constr_viol_tol"].value = 1e-3;

      // first-order
      solver.parameters ()["ipopt.derivative_test"].value = "first-order";
      solver.parameters ()["nag.verify-level"].value = 0;

      std::cout << solver << roboptim::resetindent << roboptim::iendl;

      const solver_t::result_t& result = solver.minimum ();

      roboptim::Function::vector_t parameters =
	data.outputTrajectoryReduced->parameters ();

      roboptim::Function::vector_t::Index length = data.nDofsFiltered ();
      roboptim::Function::vector_t::Index start =
	static_cast<roboptim::Function::vector_t::Index>
	(options.frameId * length);


      if (result.which () == solver_t::SOLVER_VALUE_WARNINGS)
	{
	  std::cout << "Optimization finished. Warnings have been issued\n";
	  roboptim::ResultWithWarnings result_ =
	    boost::get<roboptim::ResultWithWarnings> (result);
	  std::cerr << result << roboptim::iendl;

	  parameters.segment (start, length) = result_.x;
	}
      else if (result.which () == solver_t::SOLVER_VALUE)
	{
	  std::cout << "Optimization finished successfully.\n";
	  roboptim::Result result_ =
	    boost::get<roboptim::Result> (result);
	  std::cerr << result << roboptim::iendl;

	  parameters.segment (start, length) = result_.x;
	}
      else
	{
	  throw std::runtime_error ("Optimization failed");
	}

      data.outputTrajectoryReduced->setParameters (parameters);

      // Normalize angles in the trajectory (except base position)
      for (roboptim::Function::size_type dofId = 0;
	   dofId < data.nDofsFiltered () - 3; ++dofId)
	data.outputTrajectoryReduced->normalizeAngles (3 + dofId);
    }

  // Re-expend trajectory.
  roboptim::Function::vector_t finalTrajectoryParameters =
    data.outputTrajectory->parameters ();
  for (roboptim::Function::vector_t::Index frameId = 0;
       frameId < data.nFrames (); ++frameId)
    {
      roboptim::Function::vector_t::Index jointIdReduced = 0;
      for (roboptim::Function::vector_t::Index jointId = 0;
	   jointId < data.nDofsFull (); ++jointId)
        {
          if (data.disabledJointsConfiguration
	      [static_cast<std::size_t> (jointId)])
	    finalTrajectoryParameters[frameId * data.nDofsFull () + jointId] =
              data.outputTrajectory->parameters ()
	      [frameId * data.nDofsFiltered () + jointIdReduced++];
        }
    }
  data.outputTrajectory->setParameters (finalTrajectoryParameters);
  writeBodyMotion (options.outputFile, data.outputTrajectory);

  return 0;
}


int main (int argc, const char* argv[])
{
  try
    {
      return safeMain (argc, argv);
    }
  catch (const std::exception& e)
    {
      std::cerr << e.what () << roboptim::iendl;
      return 1;
    }
}
