// Copyright (c) 2016, Joseph Mirabel
// Authors: Joseph Mirabel (joseph.mirabel@laas.fr)
//
// This file is part of hpp-constraints.
// hpp-constraints is free software: you can redistribute it
// and/or modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation, either version
// 3 of the License, or (at your option) any later version.
//
// hpp-constraints is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Lesser Public License for more details.  You should have
// received a copy of the GNU Lesser General Public License along with
// hpp-constraints. If not, see <http://www.gnu.org/licenses/>.

#include <hpp/constraints/generic-transformation.hh>

#include <boost/math/constants/constants.hpp>

#include <hpp/fcl/math/transform.h>

#include <hpp/pinocchio/device.hh>
#include <hpp/pinocchio/joint.hh>

#include <hpp/constraints/macros.hh>

namespace hpp {
  namespace constraints {
    namespace {
      static inline size_type size (std::vector<bool> mask)
      {
        size_type res = 0;
        for (std::vector<bool>::iterator it = mask.begin (); it != mask.end ();
            ++it) {
          if (*it) ++res;
        }
        return res;
      }

      template <bool flag /* false */ > struct unary
      {
        template <bool rel, bool pos> static inline void log (
            const GenericTransformationData<rel, pos, flag>&) {}
        template <bool rel, bool pos> static inline void Jlog (
            const GenericTransformationData<rel, pos, flag>&) {}
      };
      template <> struct unary <true>
      {
        template <bool rel, bool pos> static inline void log (
            const GenericTransformationData<rel, pos, true>& d)
          {
            const matrix3_t& Rerror (d.M.rotation ());
            value_type tr = Rerror.trace();
            if (tr > 3)       d.theta = 0; // acos((3-1)/2)
            else if (tr < -1) d.theta = ::boost::math::constants::pi<value_type>(); // acos((-1-1)/2)
            else              d.theta = acos ((tr - 1)/2);
            hppDnum (info, "theta_=" << d.theta);
            assert (d.theta == d.theta);
            // From runs of tests/logarithm.cc: 1e-6 is too small.
            if (d.theta < M_PI - 1e-2) {
              const value_type t = ((d.theta > 1e-6)? d.theta / sin(d.theta) : 1) / 2;
              d.value((pos?3:0)+0) = t * (Rerror (2, 1) - Rerror (1, 2));
              d.value((pos?3:0)+1) = t * (Rerror (0, 2) - Rerror (2, 0));
              d.value((pos?3:0)+2) = t * (Rerror (1, 0) - Rerror (0, 1));
            } else {
              // 1e-2: A low value is not required since the computation is
              // using explicit formula. However, the precision of this method
              // is the square root of the precision with the antisymmetric
              // method (Nominal case).
              const value_type cphi = cos(d.theta - M_PI);
              const value_type beta  = d.theta*d.theta / ( 1 + cphi );
              const value_type tmp0 = (Rerror (0, 0) + cphi) * beta;
              const value_type tmp1 = (Rerror (1, 1) + cphi) * beta;
              const value_type tmp2 = (Rerror (2, 2) + cphi) * beta;
              d.value((pos?3:0)+0) = (Rerror (2, 1) > Rerror (1, 2) ? 1 : -1 ) * (tmp0 > 0 ? sqrt(tmp0) : 0);
              d.value((pos?3:0)+1) = (Rerror (0, 2) > Rerror (2, 0) ? 1 : -1 ) * (tmp1 > 0 ? sqrt(tmp1) : 0);
              d.value((pos?3:0)+2) = (Rerror (1, 0) > Rerror (0, 1) ? 1 : -1 ) * (tmp2 > 0 ? sqrt(tmp2) : 0);
            }
          }
        template <bool rel, bool pos> static inline void Jlog (
            const GenericTransformationData<rel, pos, true>& d)
          {
            if (d.theta < 1e-6) {
              if (d.R1isID) d.JlogXTR1inJ1.setIdentity ();
              else          d.JlogXTR1inJ1.noalias() = d.F1inJ1.rotation().derived().transpose();
            } else {
              // Jlog = alpha I
              const value_type ct = cos(d.theta), st = sin(d.theta);
              const value_type st_1mct = st/(1-ct);

              d.JlogXTR1inJ1.setZero ();
              d.JlogXTR1inJ1.diagonal().setConstant (d.theta*st_1mct);

              // Jlog += -r_{\times}/2
              d.JlogXTR1inJ1(0,1) =  d.value((pos?3:0)+2); d.JlogXTR1inJ1(1,0) = -d.value((pos?3:0)+2);
              d.JlogXTR1inJ1(0,2) = -d.value((pos?3:0)+1); d.JlogXTR1inJ1(2,0) =  d.value((pos?3:0)+1);
              d.JlogXTR1inJ1(1,2) =  d.value((pos?3:0)+0); d.JlogXTR1inJ1(2,1) = -d.value((pos?3:0)+0);
              d.JlogXTR1inJ1 /= 2;

              const value_type alpha = 1/(d.theta*d.theta) - st_1mct/(2*d.theta);
              d.JlogXTR1inJ1.noalias() += alpha * d.value.template tail<3>() * d.value.template tail<3>().transpose ();
              if (!d.R1isID)
                d.JlogXTR1inJ1 *= d.F1inJ1.rotation().derived().transpose();
            }
          } 
      };

      template <typename Data, typename Derived> void assign_if
        (bool cond, const Data& d, matrixOut_t J,
         const Eigen::MatrixBase<Derived>& rhs,
         const size_type& startRow)
      {
        if (cond) d.jacobian.template middleRows<3>(startRow)                 .noalias() = rhs;
        else               J.template middleRows<3>(startRow).leftCols(d.cols).noalias() = rhs;
      }

      template <bool lflag /*rel*/, bool rflag /*false*/> struct binary
      {
        // the first template allow us to consider relative transformation as
        // absolute when joint1 is NULL, at run time
        template <bool rel, bool pos> static inline void Jorientation (
            const GenericTransformationData<rel, pos, rflag>&, matrixOut_t) {}
        template <bool rel, bool ori> static inline void Jtranslation (
            const GenericTransformationData<rel, rflag, ori>&, matrixOut_t) {}
      };
      template <> struct binary<false, true> // Absolute
      {
        template <bool rel, bool pos> static inline void Jorientation (
            const GenericTransformationData<rel, pos, true>& d, matrixOut_t J)
        {
          assign_if(!d.fullOri, d, J,
            (d.JlogXTR1inJ1 * d.R2()) * d.J2().template bottomRows<3>(),
            d.rowOri);
        }
        template <bool rel, bool ori> static inline void Jtranslation (
            const GenericTransformationData<rel, true, ori>& d,
            matrixOut_t J)
        {
          const JointJacobian_t& J2 (d.J2());
          const matrix3_t& R2 (d.R2());
          const matrix3_t& R1inJ1 (d.F1inJ1.rotation ());

          // hpp-model: J = 1RT* ( 0Jt2 - [ 0R2 2t* ]x 0Jw2 )
          // pinocchio: J = 1RT* ( 0R2 2Jt2 - [ 0R2 2t* ]x 0R2 2Jw2 )
          if (!d.t2isZero) {
            d.tmpJac.noalias() = ( R2.colwise().cross(d.cross2)) * J2.bottomRows<3>();
            d.tmpJac.noalias() += R2 * J2.topRows<3>();
            if (d.R1isID) {
              assign_if (!d.fullPos, d, J, d.tmpJac, 0);
            } else { // Generic case
              assign_if (!d.fullPos, d, J, R1inJ1.transpose() * d.tmpJac, 0);
            }
          } else {
            if (d.R1isID)
              assign_if (!d.fullPos, d, J, R2 * J2.topRows<3>(), 0);
            else
              assign_if (!d.fullPos, d, J, (R1inJ1.transpose() * R2) * J2.topRows<3>(), 0);
          }
        }
      };
      template <> struct binary<true, true> // Relative
      {
        template <bool pos> static inline void Jorientation (
            const GenericTransformationData<true, pos, true>& d,
            matrixOut_t J)
        {
          d.tmpJac.noalias() =
                  d.R2() * d.J2().template bottomRows<3>()
                - d.R1() * d.J1().template bottomRows<3>();
          assign_if(!d.fullOri, d, J,
              d.JlogXTR1inJ1 * d.R1().transpose () * d.tmpJac,
              d.rowOri);
        }
        template <bool ori> static inline void Jtranslation (
            const GenericTransformationData<true, true, ori>& d,
            matrixOut_t J)
        {
          const JointJacobian_t& J1 (d.J1()); const JointJacobian_t& J2 (d.J2());
          const matrix3_t&       R1 (d.R1()); const matrix3_t&       R2 (d.R2());
          const matrix3_t& R1inJ1 (d.F1inJ1.rotation ());

          // J = 1RT* 0RT1 ( A + B )
          // hpp-model:
          // A = [ 0t2 - 0t1 0R2 2t* ]x 0Jw1
          // B = ( 0Jt2 - 0Jt1 - [ 0R2 2t* ]x 0Jw2 )
          // pinocchio:
          // A = [ 0t2 - 0t1 0R2 2t* ]x 0R1 1Jw1
          // B = ( 0R2 2Jt2 - 0R1 1Jt1 - [ 0R2 2t* ]x 0R2 2Jw2 )
          d.tmpJac.noalias() =
            - R1.colwise().cross(d.cross1) * J1.bottomRows<3>() // A
            + R2 * J2.topRows<3>()  // B1
            - R1 * J1.topRows<3>(); // B2
          if (!d.t2isZero)
            d.tmpJac.noalias() += R2.colwise().cross(d.cross2) * J2.bottomRows<3>(); // B3
          if (d.R1isID) assign_if(!d.fullPos, d, J,                       R1.transpose()  * d.tmpJac, 0);
          else          assign_if(!d.fullPos, d, J, (R1inJ1.transpose() * R1.transpose()) * d.tmpJac, 0);
        }
      };

      template <bool compileTimeRel /* false */, bool ori /* false */> struct relativeTransform {
        template <bool runtimeRel> static inline void run (
            const GenericTransformationData<runtimeRel, true, false>& d)
        {
          // There is no joint1
          const Transform3f& J2 = d.joint2->currentTransformation ();
          d.value.noalias() = J2.act (d.F2inJ2.translation());
          if (!d.t1isZero) d.value.noalias() -= d.F1inJ1.translation();
          if (!d.R1isID)
            d.value.applyOnTheLeft(d.F1inJ1.rotation().transpose());
        }
      };
      template <> struct relativeTransform<false, true> {
        template <bool runtimeRel, bool pos> static inline void run (
            const GenericTransformationData<runtimeRel, pos, true>& d)
        {
          const Transform3f& J2 = d.joint2->currentTransformation ();
          d.M = d.F1inJ1.actInv(J2 * d.F2inJ2);
          if (pos) d.value.template head<3>().noalias() = d.M.translation();
        }
      };
      template <> struct relativeTransform<true, true> {
        template <bool pos> static inline void run (
            const GenericTransformationData<true, pos, true>& d)
        {
          if (d.joint1 == NULL) {
            // runtime absolute reference.
            relativeTransform<false, true>::run(d);
            return;
          }
          const Transform3f& J1 = d.joint1->currentTransformation ();
          const Transform3f& J2 = d.joint2->currentTransformation ();
          d.M = d.F1inJ1.actInv(J1.actInv(J2 * d.F2inJ2));
          if (pos) d.value.template head<3>().noalias() = d.M.translation();
        }
      };
      template <> struct relativeTransform<true, false> {
        static inline void run (const GenericTransformationData<true, true, false>& d)
        {
          if (d.joint1 == NULL) {
            // runtime absolute reference.
            relativeTransform<false, false>::run(d);
            return;
          }
          const Transform3f& J2 = d.joint2->currentTransformation ();
          const Transform3f& J1 = d.joint1->currentTransformation ();
          d.value.noalias() = J2.act (d.F2inJ2.translation())
                              - J1.translation();
          d.value.applyOnTheLeft(J1.rotation().derived().transpose());

          if (!d.t1isZero) d.value.noalias() -= d.F1inJ1.translation();
          if (!d.R1isID)
            d.value.applyOnTheLeft(d.F1inJ1.rotation().derived().transpose());
        }
      };

      template <bool rel, bool pos, bool ori> struct compute
      {
        static inline void error (const GenericTransformationData<rel, pos, ori>& d)
        {
          relativeTransform<rel, ori>::run (d);
          unary<ori>::log(d);
        }

        static inline void jacobian (const GenericTransformationData<rel, pos, ori>& d,
            matrixOut_t jacobian, const std::vector<bool>& mask)
        {
          const Transform3f& J2 = d.joint2->currentTransformation ();
          const vector3_t& t2inJ2 (d.F2inJ2.translation ());
          const vector3_t& t2 (J2.translation ());
          const matrix3_t& R2 (J2.rotation ());

          if (!d.t2isZero)
            d.cross2.noalias() = R2*t2inJ2;

          unary<ori>::Jlog (d);
          hppDnum (info, "Jlog_: " << d.JlogXTR1inJ1);

          // rel:           relative known at compile time
          // d.getJoint1(): relative known at run time
          if (rel && d.getJoint1()) {
            const Transform3f& J1 = d.getJoint1()->currentTransformation ();
            const vector3_t& t1 (J1.translation ());
            d.cross1.noalias() = d.cross2 + t2 - t1;
            binary<rel, pos>::Jtranslation (d, jacobian);
            binary<rel, ori>::Jorientation (d, jacobian);
          } else {
            d.cross1.noalias() = d.cross2 + t2;
            binary<false, pos>::Jtranslation (d, jacobian);
            binary<false, ori>::Jorientation (d, jacobian);
          }

          // Copy necessary rows.
          size_type index=0;
          const std::size_t lPos = (pos?3:0), lOri = (ori?3:0);
          if (!d.fullPos) {
            for (size_type i=0; i<lPos; ++i) {
              if (mask [i]) {
                jacobian.row(index).leftCols(d.cols).noalias() = d.jacobian.row(i); ++index;
              }
            }
          } else index = lPos;
          if (!d.fullOri) {
            for (size_type i=lPos; i<lPos+lOri; ++i) {
              if (mask [i]) {
                jacobian.row(index).leftCols(d.cols).noalias() = d.jacobian.row(i); ++index;
              }
            }
          }
          jacobian.rightCols(jacobian.cols()-d.cols).setZero();
        }
      };
    }

    template <int _Options> typename GenericTransformation<_Options>::Ptr_t
      GenericTransformation<_Options>::create
    (const std::string& name, const DevicePtr_t& robot,
     const JointPtr_t& joint2,
     const Transform3f& reference, std::vector <bool> mask)
    {
      GenericTransformation<_Options>* ptr =
        new GenericTransformation<_Options> (name, robot, mask);
      ptr->joint1 (JointPtr_t());
      ptr->joint2 (joint2);
      ptr->reference (reference);
      Ptr_t shPtr (ptr);
      ptr->init (shPtr);
      return shPtr;
    }

    template <int _Options> typename GenericTransformation<_Options>::Ptr_t
      GenericTransformation<_Options>::create
    (const std::string& name, const DevicePtr_t& robot,
     /* World frame          */ const JointPtr_t& joint2,
     const Transform3f& frame2, const Transform3f& frame1,
     std::vector <bool> mask)
    {
      GenericTransformation<_Options>* ptr =
        new GenericTransformation<_Options> (name, robot, mask);
      ptr->joint1 (JointPtr_t());
      ptr->joint2 (joint2);
      ptr->frame1InJoint1 (frame1);
      ptr->frame2InJoint2 (frame2);
      Ptr_t shPtr (ptr);
      return shPtr;
    }

    template <int _Options> typename GenericTransformation<_Options>::Ptr_t
      GenericTransformation<_Options>::create
    (const std::string& name, const DevicePtr_t& robot,
     const JointPtr_t& joint1, const JointPtr_t& joint2,
     const Transform3f& reference, std::vector <bool> mask)
    {
      GenericTransformation<_Options>* ptr =
        new GenericTransformation<_Options> (name, robot, mask);
      ptr->joint1 (joint1);
      ptr->joint2 (joint2);
      ptr->reference (reference);
      Ptr_t shPtr (ptr);
      ptr->init (shPtr);
      return shPtr;
    }

    template <int _Options> typename GenericTransformation<_Options>::Ptr_t
      GenericTransformation<_Options>::create
    (const std::string& name, const DevicePtr_t& robot,
     const JointPtr_t& joint1, const JointPtr_t& joint2,
     const Transform3f& frame1, const Transform3f& frame2,
     std::vector <bool> mask)
    {
      GenericTransformation<_Options>* ptr =
        new GenericTransformation<_Options> (name, robot, mask);
      ptr->joint1 (joint1);
      ptr->joint2 (joint2);
      ptr->frame1InJoint1 (frame1);
      ptr->frame2InJoint2 (frame2);
      Ptr_t shPtr (ptr);
      return shPtr;
    }

    template <int _Options>
      GenericTransformation<_Options>::GenericTransformation
      (const std::string& name, const DevicePtr_t& robot,
       std::vector <bool> mask) :
        DifferentiableFunction (robot->configSize (), robot->numberDof (),
			      size (mask), name),
      robot_ (robot), d_(robot->numberDof()-robot->extraConfigSpace().dimension()), mask_ (mask)
    {
      assert(mask.size()==ValueSize);
      std::size_t iOri = 0;
      d_.rowOri = 0;
      if (ComputePosition) {
        for (size_type i=0; i<3; ++i) if (mask_[i]) d_.rowOri++;
        d_.fullPos = (d_.rowOri==3);
        iOri = 3;
      } else d_.fullPos = false;
      if (ComputeOrientation)
        d_.fullOri = mask_[iOri + 0] && mask_[iOri + 1] && mask_[iOri + 2];
      else d_.fullOri = false;
    }

    template <int _Options>
    inline void GenericTransformation<_Options>::computeError (const ConfigurationIn_t& argument) const
    {
      hppDnum (info, "argument=" << argument.transpose ());
      if (argument.size () != latestArgument_.size () ||
	  argument != latestArgument_) {
	robot_->currentConfiguration (argument);
	robot_->computeForwardKinematics ();
        compute<IsRelative, ComputePosition, ComputeOrientation>::error (d_);
	latestArgument_.noalias() = argument;
      }
    }

    template <int _Options>
    void GenericTransformation<_Options>::impl_compute (vectorOut_t result,
					       ConfigurationIn_t argument)
      const throw ()
    {
      computeError (argument);
      size_type index=0;
      for (size_type i=0; i<ValueSize; ++i) {
	if (mask_ [i]) {
	  result [index] = d_.value[i]; ++index;
	}
      }
    }

    template <int _Options>
    void GenericTransformation<_Options>::impl_jacobian
    (matrixOut_t jacobian, ConfigurationIn_t arg) const throw ()
    {
      computeError (arg);
      compute<IsRelative, ComputePosition, ComputeOrientation>::jacobian (d_, jacobian, mask_);
    }

    /// Force instanciation of relevant classes
    template class GenericTransformation<               PositionBit | OrientationBit >;
    template class GenericTransformation<               PositionBit                  >;
    template class GenericTransformation<                             OrientationBit >;
    template class GenericTransformation< RelativeBit | PositionBit | OrientationBit >;
    template class GenericTransformation< RelativeBit | PositionBit                  >;
    template class GenericTransformation< RelativeBit |               OrientationBit >;
  } // namespace constraints
} // namespace hpp