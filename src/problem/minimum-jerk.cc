#include <vector>
#include <boost/make_shared.hpp>

#include <roboptim/core/numeric-linear-function.hh>
#include <roboptim/core/linear-function.hh>
#include <roboptim/core/optimization-logger.hh>
#include <roboptim/core/problem.hh>
#include <roboptim/core/solver.hh>
#include <roboptim/core/solver-factory.hh>
#include <roboptim/core/filter/chain.hh>
#include <roboptim/core/filter/selection.hh>
#include <roboptim/core/filter/split.hh>
#include <roboptim/trajectory/state-function.hh>
#include <roboptim/trajectory/vector-interpolation.hh>

#include <roboptim/retargeting/function/forward-geometry/choreonoid.hh>
#include <roboptim/retargeting/function/torque/choreonoid.hh>
#include <roboptim/retargeting/function/torque/metapod.hh>
#include <roboptim/retargeting/function/zmp/choreonoid.hh>
#include <roboptim/retargeting/function/zmp/metapod.hh>


#include "roboptim/retargeting/problem/minimum-jerk.hh"

#include <cnoid/Body>
#include "../model/hrp4g2.hh"


boost::array<double, 6 + 44> standardPose = {{
    0, 0, 0.6, 0, 0, 0, //FIXME: double check robot height

    0, 0, -25, 50, -25, 0, 0,
    0, 0, -25, 50, -25, 0, 0,
    0, 0, 0,
    0, 0, 0,
    -1.0, 1.0, 0, 0, 0, -1.0, 1.0, -1.0,
    5, -10, 0, -15, 0,  10,  1.0, 0,
    5,  10, 0, -15, 0, -10, -1.0, 0
  }};

namespace roboptim
{
  namespace retargeting
  {
    template <typename T, typename U>
    class MinimumJerkOptimizationLogger : public OptimizationLogger<T>
    {
    public:
      typedef T solver_t;
      typedef typename solver_t::problem_t problem_t;
      typedef typename solver_t::problem_t::value_type value_type;
      typedef typename solver_t::problem_t::vector_t vector_t;
      typedef typename solver_t::problem_t::function_t::discreteInterval_t
      discreteInterval_t;

      explicit MinimumJerkOptimizationLogger
      (solver_t& solver, const boost::filesystem::path& path,
       discreteInterval_t discreteInterval,
       boost::shared_ptr<Trajectory<3> > trajectory,
       boost::shared_ptr<ZMP<U> > zmp,
       typename solver_t::callback_t additionalCallback)
	: OptimizationLogger<T> (solver, path),
	  discreteInterval_ (discreteInterval),
	  trajectory_ (trajectory),
	  zmp_ (zmp),
	  additionalCallback_ (additionalCallback)
      {
      }

      ~MinimumJerkOptimizationLogger () throw ()
      {}
    protected:
      virtual
      void perIterationCallbackUnsafe (const typename solver_t::vector_t& x,
				       const typename solver_t::problem_t& pb)
      {
	OptimizationLogger<T>::perIterationCallbackUnsafe (x, pb);

	boost::filesystem::path path = this->path ();
	const boost::filesystem::path iterationPath =
	  path / (boost::format ("iteration-%d")
		  % this->callbackCallId ()).str ();

	// Log ZMP
	if (zmp_)
	  {
	    boost::filesystem::ofstream streamZmp (iterationPath / "zmp.csv");
	    streamZmp << "ZMP X, ZMP Y\n";

	    const value_type tmin = boost::get<0> (discreteInterval_);
	    const value_type tmax = boost::get<1> (discreteInterval_);
	    const value_type dt = boost::get<2> (discreteInterval_);

	    for (value_type t = tmin; t + dt < tmax; t += dt)
	      {
		trajectory_->setParameters (x);
		vector_t state = trajectory_->state (t, 2);
		vector_t zmp = (*zmp_) (state);
		streamZmp << zmp[0] << ", " << zmp[1] << "\n";
	      }
	    streamZmp << "\n";
	  }

	// Call additional callback.
	if (additionalCallback_)
	  additionalCallback_ (x, pb);
      }

    private:
      discreteInterval_t discreteInterval_;
      boost::shared_ptr<Trajectory<3> > trajectory_;
      boost::shared_ptr<ZMP<U> > zmp_;
      typename solver_t::callback_t additionalCallback_;
    };

    namespace problem
    {
      log4cxx::LoggerPtr MinimumJerk::logger
      (log4cxx::Logger::getLogger("roboptim.retargeting.MinimumJerk"));

      MinimumJerk::MinimumJerk
      (cnoid::BodyPtr robot,
       size_type nFrames,
       value_type dt,
       size_type nNodes,
       bool enableFreezeFrame,
       bool enableVelocity,
       bool enableFeetPositions,
       bool enableCollision,
       bool enableTorque,
       bool enableZmp,
       const std::string& solverName,
       std::vector<bool> enabledDofs,
       solver_t::callback_t additionalCallback)
	: robot_ (robot),
	  nFrames_ (nFrames),
	  dt_ (dt),
	  nNodes_ (nNodes),
	  tmin_ (0.),
	  tmax_ (dt_ * static_cast<value_type> (nFrames_)),
	  dofId_ (6 + 28),
	  init_ (standardPose[dofId_] * M_PI / 180.),
	  goal_ (robot->joint (dofId_ - 6)->q_upper () - 0.01),
	  nDofs_ (standardPose.size ()),
	  interval_ (tmin_, tmax_, 0.01),
	  cost_ (),
	  positions_ (2),
	  torque_ (),
	  zmp_ (),
	  trajectoryConstraints_ (),
	  problem_ (),
	  result_ (),
	  solverName_ (solverName),
	  additionalCallback_ (additionalCallback),
	  enabledDofs_ (enabledDofs)
      {}

      MinimumJerkShPtr_t
      MinimumJerk::buildVectorInterpolationBasedOptimizationProblem
      (cnoid::BodyPtr robot,
       size_type nFrames,
       value_type dt,
       bool enableFreezeFrame,
       bool enableVelocity,
       bool enableFeetPositions,
       bool enableCollision,
       bool enableTorque,
       bool enableZmp,
       const std::string& solverName,
       std::vector<bool> enabledDofs,
       solver_t::callback_t additionalCallback)
      {
	MinimumJerkShPtr_t minimumJerk
	  (new MinimumJerk
	   (robot,
	    nFrames,
	    dt,
	    0,
	    enableFreezeFrame,
	    enableVelocity,
	    enableFeetPositions,
	    enableCollision,
	    enableTorque,
	    enableZmp,
	    solverName,
	    enabledDofs,
	    additionalCallback));

        // Compute the initial trajectory (whole body)
	vector_t initialTrajectory (minimumJerk->nFrames_ * standardPose.size ());
	initialTrajectory.setZero ();

	vector_t x (4);
	x[0] = minimumJerk->init_;
	x[1] = minimumJerk->goal_;
	x[2] = x[3] = 0.;

	boost::shared_ptr<MinimumJerkTrajectory<EigenMatrixDense> >
	  minimumJerkTrajectory =
	  boost::make_shared<MinimumJerkTrajectory<EigenMatrixDense> >
	  ();
	minimumJerkTrajectory->setParameters (x);

	// Build the starting point
	value_type dtMinimumJerk = (1. - 0.) / minimumJerk->nFrames_;
	for (std::size_t frameId = 0; frameId < minimumJerk->nFrames_; ++frameId)
	  {
	    for (std::size_t dof = 0; dof < minimumJerk->nDofs_; ++dof)
	      initialTrajectory[frameId * minimumJerk->nDofs_ + dof] =
		(dof < 6)
		? standardPose[dof] : standardPose[dof] * M_PI / 180.;

	    initialTrajectory[frameId * minimumJerk->nDofs_
			      + minimumJerk->dofId_] =
	      (*minimumJerkTrajectory)
	      (static_cast<value_type> (frameId) * dtMinimumJerk)[0];
	  }

	// Build a trajectory over the starting point for display
	boost::shared_ptr<Trajectory<3> >
	  initialTrajectoryFct =
	  vectorInterpolation
	  (initialTrajectory,
	   static_cast<size_type> (minimumJerk->nDofs_),
	   minimumJerk->dt_);

	// Build the cost function as the difference between the
	// reference trajectory and the current trajectory.
	minimumJerk->cost_ = boost::make_shared<CostReferenceTrajectory<EigenMatrixDense> >
	  (initialTrajectoryFct, minimumJerk->dofId_, minimumJerk->dt_);

	// Clone the vector interpolation object so that it can be used by
	// constraints
	minimumJerk->trajectoryConstraints_ =
	  vectorInterpolation
	  (initialTrajectory,
	   static_cast<size_type> (minimumJerk->nDofs_),
	   minimumJerk->dt_);

	// Create problem.
	minimumJerk->problem_ = boost::make_shared<problem_t> (*minimumJerk->cost_);
	minimumJerk->problem_->startingPoint () = initialTrajectory;

	minimumJerk->addConstraints
	  (enableFreezeFrame,
	   enableVelocity,
	   enableFeetPositions,
	   enableCollision,
	   enableTorque,
	   enableZmp);

	return minimumJerk;
      }

      MinimumJerkShPtr_t
      MinimumJerk::buildSplineBasedOptimizationProblem
      (cnoid::BodyPtr robot,
       size_type nFrames,
       size_type nNodes,
       bool enableFreezeFrame,
       bool enableVelocity,
       bool enableFeetPositions,
       bool enableCollision,
       bool enableTorque,
       bool enableZmp,
       const std::string& solverName,
       std::vector<bool> enabledDofs,
       solver_t::callback_t additionalCallback)
      {
	MinimumJerkShPtr_t minimumJerk
	  (new MinimumJerk
	   (robot,
	    nFrames,
	    0,
	    nNodes,
	    enableFreezeFrame,
	    enableVelocity,
	    enableFeetPositions,
	    enableCollision,
	    enableTorque,
	    enableZmp,
	    solverName,
	    enabledDofs,
	    additionalCallback));

	// minimumJerk->addConstraints
	//   (enableFreezeFrame,
	//    enableVelocity,
	//    enableFeetPositions,
	//    enableCollision,
	//    enableTorque,
	//    enableZmp);

	return minimumJerk;
      }

      void
      MinimumJerk::addConstraints
      (bool enableFreezeFrame,
       bool enableVelocity,
       bool enableFeetPositions,
       bool enableCollision,
       bool enableTorque,
       bool enableZmp)
      {
	// Should we use Metapod or Choreonoid?
	bool useMetapodTorque = true;
	bool useMetapodZmp = false;

	// Bound joint positions.
	{
	  for (std::size_t frameId = 0; frameId < nFrames_; ++frameId)
	    for (std::size_t jointId = 0;
		 jointId < robot_->numJoints (); ++jointId)
	      problem_->argumentBounds ()
		[frameId * (6 + robot_->numJoints ()) + 6 + jointId] =
		MinimumJerkTrajectory<EigenMatrixDense>::makeInterval
		(robot_->joint (jointId)->q_lower (),
		 robot_->joint (jointId)->q_upper ());

	  // Fix disabled DOFs
	  for (std::size_t frameId = 0; frameId < nFrames_; ++frameId)
	    for (std::size_t jointId = 0;
		 jointId < 6 + robot_->numJoints (); ++jointId)
	      if (!enabledDofs_[jointId])
	      problem_->argumentBounds ()
		[frameId * (6 + robot_->numJoints ()) + jointId] =
		MinimumJerkTrajectory<EigenMatrixDense>::makeInterval
		(standardPose[jointId],
		 standardPose[jointId]);
	}

	// Freeze first frame
	if (enableFreezeFrame)
	  {
	    matrix_t A (6 + robot_->numJoints (),
			nFrames_ * (6 + robot_->numJoints ()));
	    A.setZero ();
	    A.block (0, 0,
		     6 + robot_->numJoints (),
		     6 + robot_->numJoints ()).setIdentity ();
	    vector_t b (6 + robot_->numJoints ());
	    for (std::size_t jointId = 0; jointId < b.size (); ++jointId)
	      b[jointId] = (jointId < 6)
		? -standardPose[jointId] : -standardPose[jointId] * M_PI / 180.;

	    freeze_ =
	      boost::make_shared<
		GenericNumericLinearFunction<EigenMatrixDense> >
	      (A, b);

	    std::vector<interval_t> freezeBounds (6 + robot_->numJoints ());
	    for (std::size_t jointId = 0;
		 jointId < 6 + robot_->numJoints (); ++jointId)
	      freezeBounds[jointId] = Function::makeInterval (0., 0.);

	    std::vector<value_type> freezeScales (6 + robot_->numJoints ());
	    for (std::size_t jointId = 0;
		 jointId < 6 + robot_->numJoints (); ++jointId)
	      freezeScales[jointId] = 1.;

	    problem_->addConstraint (freeze_, freezeBounds, freezeScales);
	  }

	if (enableFeetPositions)
	  {
	    unsigned nConstraints = nFrames_ - 2;

	    vector_t qInitial (6 + robot_->numJoints ());
	    for (std::size_t dofId = 0; dofId < qInitial.size (); ++dofId)
	      qInitial[dofId] = standardPose[dofId];

	    typedef ForwardGeometryChoreonoid<EigenMatrixDense>
	      forwardGeometry_t;

	    // Left foot.
	    positions_[0] =
	      boost::make_shared<forwardGeometry_t>
	      (robot_, std::string ("L_TOE_P"));

	    forwardGeometry_t::vector_t leftFootPosition =
	      (*positions_[0]) (qInitial);
	    std::vector<interval_t> leftFootBounds (leftFootPosition.size ());
	    for (std::size_t i = 0; i < leftFootPosition.size (); ++i)
	      leftFootBounds[i] = forwardGeometry_t::makeInterval
		(leftFootPosition[i], leftFootPosition[i]);

	    std::vector<value_type> leftFootScales (6);
	    for (std::size_t i = 0; i < leftFootScales.size (); ++i)
	      leftFootScales[i] = 1.;

	    roboptim::StateFunction<Trajectory<3> >::addToProblem
	      (*trajectoryConstraints_, positions_[0], 0,
	       *problem_, leftFootBounds, leftFootScales, nConstraints);

	    // Right foot.
	    positions_[1] =
	      boost::make_shared<forwardGeometry_t>
	      (robot_, std::string ("R_TOE_P"));

	    forwardGeometry_t::vector_t rightFootPosition =
	      (*positions_[1]) (qInitial);
	    std::vector<interval_t> rightFootBounds (rightFootPosition.size ());
	    for (std::size_t i = 0; i < rightFootPosition.size (); ++i)
	      rightFootBounds[i] = forwardGeometry_t::makeInterval
		(rightFootPosition[i], rightFootPosition[i]);
	    std::vector<value_type> rightFootScales (rightFootPosition.size ());
	    for (std::size_t i = 0; i < rightFootScales.size (); ++i)
	      rightFootScales[i] = 1.;

	    roboptim::StateFunction<Trajectory<3> >::addToProblem
	      (*trajectoryConstraints_, positions_[1], 0,
	       *problem_, rightFootBounds, rightFootScales, nConstraints);
	  }

	boost::shared_ptr<GenericLinearFunction<EigenMatrixDense> >
	  velocityOneFrame;
	if (enableVelocity)
	  {
	    unsigned nConstraints = nFrames_ - 2;

	    matrix_t A (robot_->numJoints (),
			2 * (6 + robot_->numJoints ()));
	    A.setZero ();
	    A.block
	      (0, 6 + robot_->numJoints (),
	       robot_->numJoints (), robot_->numJoints ()).setIdentity ();
	    vector_t b (robot_->numJoints ());
	    b.setZero ();
	    velocityOneFrame =
	      boost::make_shared<
		GenericNumericLinearFunction<EigenMatrixDense> >
	      (A, b);

	    std::vector<interval_t> velocityBounds (robot_->numJoints ());
	    for (std::size_t jointId = 0;
		 jointId < robot_->numJoints (); ++jointId)
	      velocityBounds[jointId] = Function::makeInterval
		(robot_->joint (jointId)->dq_lower (),
		 robot_->joint (jointId)->dq_upper ());

	    std::vector<value_type> velocityScales (robot_->numJoints ());
	    for (std::size_t jointId = 0;
		 jointId < robot_->numJoints (); ++jointId)
	      velocityScales[jointId] = 1.;

	    roboptim::StateFunction<Trajectory<3> >::addToProblem
	      (*trajectoryConstraints_, velocityOneFrame, 1,
	       *problem_, velocityBounds, velocityScales, nConstraints);
	  }
	if (enableTorque)
	  {
	    unsigned nConstraints = nFrames_ - 2;
	    std::vector<interval_t> torqueBounds (nDofs_);

	    // FIXME: should we constraint the free floating?
	    // static const double g = 9.81;
	    // torqueBounds[0] = interval_t (0., 0.);
	    // torqueBounds[1] = interval_t (0., 0.);
	    // torqueBounds[2] = interval_t (-robot_->mass () * g, robot_->mass () * g);
	    // torqueBounds[3] = interval_t (0., 0.);
	    // torqueBounds[4] = interval_t (0., 0.);
	    // torqueBounds[5] = interval_t (0., 0.);
	    torqueBounds[6 + 0] = std::make_pair (-63.55, 63.55); // R_HIP_Y
	    torqueBounds[6 + 1] = std::make_pair (-186.21, 186.21); // R_HIP_R
	    torqueBounds[6 + 2] = std::make_pair (-95.18, 95.18); // R_HIP_P
	    torqueBounds[6 + 3] = std::make_pair (-145.98, 145.98); // R_KNEE_P
	    torqueBounds[6 + 4] = std::make_pair (-111.42, 111.42); // R_ANKLE_P
	    torqueBounds[6 + 5] = std::make_pair (-75.11, 75.11); // R_ANKLE_R
	    torqueBounds[6 + 6] = std::make_pair (-52.78, 52.78); // R_TOE_P
	    torqueBounds[6 + 7] = std::make_pair (-186.21, 186.21); // L_HIP_R
	    torqueBounds[6 + 9] = std::make_pair (-95.18, 95.18); // L_HIP_P
	    torqueBounds[6 + 10] = std::make_pair (-145.98, 145.98); // L_KNEE_P
	    torqueBounds[6 + 11] = std::make_pair (-111.42, 111.42); // L_ANKLE_P
	    torqueBounds[6 + 12] = std::make_pair (-75.11, 75.11); // L_ANKLE_R
	    torqueBounds[6 + 13] = std::make_pair (-52.78, 52.78); // L_TOE_P
	    torqueBounds[6 + 14] = std::make_pair (-97.53, 97.53); // CHEST_P
	    torqueBounds[6 + 15] = std::make_pair (-96.93, 96.93); // CHEST_R
	    torqueBounds[6 + 16] = std::make_pair (-90.97, 90.97); // CHEST_Y
	    torqueBounds[6 + 17] = std::make_pair (-17.59, 17.59); // NECK_Y
	    torqueBounds[6 + 18] = std::make_pair (-17.59, 17.59); // NECK_R
	    torqueBounds[6 + 19] = std::make_pair (-17.59, 17.59); // NECK_P
	    torqueBounds[6 + 20] = std::make_pair (-5.26, 5.26); // EYEBROW_P
	    torqueBounds[6 + 21] = std::make_pair (-0.71, 0.71); // EYELID_P
	    torqueBounds[6 + 22] = std::make_pair (-0.84, 0.84); // EYE_P
	    torqueBounds[6 + 23] = std::make_pair (-0.42, 0.42); // EYE_Y
	    torqueBounds[6 + 24] = std::make_pair (-4.72, 4.72); // MOUTH_P
	    torqueBounds[6 + 25] = std::make_pair (-0.22, 0.22); // LOWERLIP_P
	    torqueBounds[6 + 26] = std::make_pair (-0.29, 0.29); // UPPERLIP_P
	    torqueBounds[6 + 27] = std::make_pair (-5.9, 5.9); // CHEEK_P
	    torqueBounds[6 + 28] = std::make_pair (-181.74, 181.74); // R_SHOULDER_P
	    torqueBounds[6 + 29] = std::make_pair (-62.83, 62.83); // R_SHOULDER_R
	    torqueBounds[6 + 30] = std::make_pair (-20.47, 20.47); // R_SHOULDER_Y
	    torqueBounds[6 + 31] = std::make_pair (-54.46, 54.46); // R_ELBOW_P
	    torqueBounds[6 + 32] = std::make_pair (-6.33, 6.33); // R_WRIST_Y
	    torqueBounds[6 + 33] = std::make_pair (-6.33, 6.33); // R_WRIST_R
	    torqueBounds[6 + 34] = std::make_pair (-0.77, 0.77); // R_HAND_J0
	    torqueBounds[6 + 35] = std::make_pair (-1.16, 1.16); // R_HAND_J1
	    torqueBounds[6 + 36] = std::make_pair (-181.74, 181.74); // L_SHOULDER_P
	    torqueBounds[6 + 37] = std::make_pair (-62.83, 62.83); // L_SHOULDER_R
	    torqueBounds[6 + 38] = std::make_pair (-20.47, 20.47); // L_SHOULDER_Y
	    torqueBounds[6 + 39] = std::make_pair (-54.46, 54.46); // L_ELBOW_P
	    torqueBounds[6 + 40] = std::make_pair (-6.33, 6.33); // L_WRIST_Y
	    torqueBounds[6 + 41] = std::make_pair (-6.33, 6.33); // L_WRIST_R
	    torqueBounds[6 + 42] = std::make_pair (-0.77, 0.77); // L_HAND_J0
	    torqueBounds[6 + 43] = std::make_pair (-1.16, 1.16); // L_HAND_J1

	    std::vector<value_type> torqueScales (nDofs_);
	    for (std::size_t i = 0; i < nDofs_; ++i)
	      torqueScales[i] = 1.;

	    if (useMetapodTorque)
	      torque_ =
		boost::make_shared<TorqueMetapod<
		  EigenMatrixDense, metapod::hrp4g2<double> > > ();
	    else
	      torque_ =
		boost::make_shared<TorqueChoreonoid<EigenMatrixDense> > (robot_);

	    roboptim::StateFunction<Trajectory<3> >::addToProblem
	      (*trajectoryConstraints_, torque_, 2,
	       *problem_, torqueBounds, torqueScales, nConstraints);
	  }
	if (enableZmp)
	  {
	    unsigned nConstraints = nFrames_ - 2;
	    value_type soleX = 0.03;
	    value_type soleY = 0.;
	    value_type soleLength = 0.2; //FIXME:
	    value_type soleWidth = 0.1; //FIXME:

	    std::vector<interval_t> zmpBounds (2);
	    zmpBounds[0] = interval_t (soleX - .5 * soleLength,
				       soleX + .5 * soleLength);
	    zmpBounds[1] = interval_t (soleY - .5 * soleWidth,
				       soleY + .5 * soleWidth);

	    std::vector<value_type> zmpScales (2);
	    zmpScales[0] = 1.;
	    zmpScales[1] = 1.;

	    if (useMetapodZmp)
	      zmp_ =
		boost::make_shared<
		  ZMPMetapod<EigenMatrixDense, metapod::hrp4g2<double> > > ();
	    else
	      zmp_ =
		boost::make_shared<ZMPChoreonoid<EigenMatrixDense> > (robot_);

	    roboptim::StateFunction<Trajectory<3> >::addToProblem
	      (*trajectoryConstraints_, zmp_, 2,
	       *problem_, zmpBounds, zmpScales, nConstraints);
	  }
    }

      MinimumJerk::~MinimumJerk ()
      {
      }

      void
      MinimumJerk::solve ()
      {
	roboptim::SolverFactory<solver_t> factory (solverName_, *problem_);
	solver_t& solver = factory ();

	boost::shared_ptr<Trajectory<3> >
	  trajectory =
	  vectorInterpolation
	  (*problem_->startingPoint (),
	   static_cast<size_type> (nDofs_), dt_);
	MinimumJerkOptimizationLogger<solver_t, EigenMatrixDense>
	  optimizationLogger
	  (solver, "/tmp/minimum-jerk-problem",
	   interval_, trajectory, zmp_, additionalCallback_);

	// Set solver parameters.
	solver.parameters ()["max-iterations"].value = 100;
	solver.parameters ()["ipopt.output_file"].value =
	  "/tmp/ipopt.log";
	solver.parameters ()["ipopt.expect_infeasible_problem"].value = "yes";
	solver.parameters ()["ipopt.nlp_scaling_method"].value = "none";
	solver.parameters ()["nag.verify-level"].value = 0;

	std::cerr << solver << std::endl;

	LOG4CXX_INFO (logger, "Solver:\n" << solver);
	LOG4CXX_DEBUG(logger, "start solving...");
	solver.solve ();
	LOG4CXX_DEBUG(logger, "problem solved.");
	result_ = solver.minimum ();
      }

    } // end of namespace problem.
  } // end of namespace retargeting.
} // end of namespace roboptim.