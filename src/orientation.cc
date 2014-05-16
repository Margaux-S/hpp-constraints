//
// Copyright (c) 2014 CNRS
// Authors: Florent Lamiraux
//
//
// This file is part of hpp-constraints.
// hpp-constraints is free software: you can redistribute it
// and/or modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation, either version
// 3 of the License, or (at your option) any later version.
//
// hpp-constraints is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Lesser Public License for more details. You should have
// received a copy of the GNU Lesser General Public License along with
// hpp-constraints. If not, see
// <http://www.gnu.org/licenses/>.

#include <hpp/model/device.hh>
#include <hpp/model/fcl-to-eigen.hh>
#include <hpp/model/joint.hh>
#include <hpp/constraints/orientation.hh>

namespace hpp {
  namespace constraints {

    static vector3_t zero3d (0, 0, 0);

    size_type Orientation::size (std::vector<bool> mask)
    {
      size_type res = 0;
      if (mask [0]) ++res;
      if (mask [1]) ++res;
      if (mask [2]) ++res;
      return res;
    }

    void computeJlog (const double& theta, vectorIn_t r,
		      eigen::matrix3_t& Jlog)
    {
      if (theta < 1e-6)
	Jlog.setIdentity ();
      else {
	Jlog.setZero ();
	// Jlog = alpha I
	double alpha = .5*theta*sin(theta)/(1-cos (theta));
	Jlog (0,0) = Jlog (1,1) = Jlog (2,2) = alpha;
	// Jlog += -r_{\times}/2
	Jlog (0,1) = .5*r [2]; Jlog (1,0) = -.5*r [2];
	Jlog (0,2) = -.5*r [1]; Jlog (2,0) = .5*r [1];
	Jlog (1,2) = .5*r [0]; Jlog (2,1) = -.5*r [0];
	alpha = 1/(theta*theta) - sin(theta)/(2*theta*(1-cos(theta)));
	Jlog += alpha * r * r.transpose ();
      }
    }

    OrientationPtr_t Orientation::create (const DevicePtr_t& robot,
					  const JointPtr_t& joint,
					  const matrix3_t& reference,
					  std::vector <bool> mask)
    {
      Orientation* ptr = new Orientation (robot, joint, reference, mask);
      OrientationPtr_t shPtr (ptr);
      return shPtr;
    }
    Orientation::Orientation (const DevicePtr_t& robot, const JointPtr_t& joint,
			      const matrix3_t& reference,
			      std::vector <bool> mask) :
      DifferentiableFunction (robot->configSize (), robot->numberDof (),
			      size (mask), "Orientation"),
      robot_ (robot), joint_ (joint), reference_ (reference),
      mask_ (mask), r_ (3), Jlog_ (),
      jacobian_ (3, robot->numberDof ())
    {
    }

    void Orientation::computeError (vectorOut_t result,
				    ConfigurationIn_t argument,
				    double& theta, std::vector<bool> mask) const
    {
      robot_->currentConfiguration (argument);
      robot_->computeForwardKinematics ();
      const Transform3f& M = joint_->currentTransformation ();
      fcl::Matrix3f RT (M.getRotation ()); RT.transpose ();
      Rerror_ = RT * reference_;
      double tr = Rerror_ (0, 0) + Rerror_ (1, 1) + Rerror_ (2, 2);
      if (tr > 1) tr = 1;
      if (tr < -1) tr = -1;
      theta = acos ((tr - 1)/2);
      size_type index = 0;
      if (mask [0]) {
	result [index] = theta*(Rerror_ (2, 1) - Rerror_ (1, 2))/(2*sin(theta));
	++index;
      }
      if (mask [1]) {
	result [index] = theta*(Rerror_ (0, 2) - Rerror_ (2, 0))/(2*sin(theta));
	++index;
      }
      if (mask [2]) {
	result [index] = theta*(Rerror_ (1, 0) - Rerror_ (0, 1))/(2*sin(theta));
      }
    }

    void Orientation::impl_compute (vectorOut_t result,
				    ConfigurationIn_t argument)
      const throw ()
    {
      double theta;
      computeError (result, argument, theta, mask_);
    }
    void Orientation::impl_jacobian (matrixOut_t jacobian,
				     ConfigurationIn_t arg) const throw ()
    {
      const Transform3f& M = joint_->currentTransformation ();
      fcl::Matrix3f RT (M.getRotation ()); RT.transpose ();
      // Compute vector r
      double theta;
      computeError (r_, arg, theta, boost::assign::list_of (true)(true)(true));
      assert (theta >= 0);
      if (theta < 1e-3) {
	Jlog_.setIdentity ();
      } else {
	computeJlog (theta, r_, Jlog_);
      }
      const JointJacobian_t& Jjoint (joint_->jacobian ());
      jacobian_ = -Jlog_ * RT * Jjoint.bottomRows (3);
      size_type index = 0;
      if (mask_ [0]) {
	jacobian.row (index) = jacobian_.row (0); ++index;
      }
      if (mask_ [1]) {
	jacobian.row (index) = jacobian_.row (1); ++index;
      }
      if (mask_ [2]) {
	jacobian.row (index) = jacobian_.row (2);
      }
    }

  } // namespace constraints
} // namespace hpp
