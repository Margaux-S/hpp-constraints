// Copyright (c) 2017, Joseph Mirabel
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

#include <hpp/constraints/hybrid-solver.hh>
#include <hpp/constraints/impl/hybrid-solver.hh>
#include <hpp/constraints/impl/iterative-solver.hh>

#include <hpp/constraints/svd.hh>
#include <hpp/constraints/macros.hh>

Eigen::IOFormat IPythonFormat (Eigen::FullPrecision, 0, ", ", ",\n", "[", "]", "numpy.array([\n", "])\n");

namespace hpp {
  namespace constraints {
    namespace lineSearch {
      template bool Backtracking::operator() (const HybridSolver& solver, vectorOut_t arg, vectorOut_t darg);

      template bool FixedSequence::operator() (const HybridSolver& solver, vectorOut_t arg, vectorOut_t darg);

      template bool ErrorNormBased::operator() (const HybridSolver& solver, vectorOut_t arg, vectorOut_t darg);
    }

    void HybridSolver::explicitSolverHasChanged()
    {
      reduction(explicit_.inDers());
    }

    void HybridSolver::updateJacobian (vectorIn_t arg) const
    {
      // Compute Je_
      explicit_.jacobian(JeExpanded_, arg);
      Je_ = explicit_.viewJacobian(JeExpanded_);

      hppDnum (info, "Jacobian of explicit system is \n" << Je_.format(IPythonFormat));

      for (std::size_t i = 0; i < stacks_.size (); ++i) {
        Data& d = datas_[i];
        hppDnum (info, "Jacobian of stack " << i << " before update: \n" << d.reducedJ.format(IPythonFormat));
        hppDnum (info, "Jacobian of explicit variable of stack " << i << ": \n" << explicit_.outDers().rviewTranspose(d.jacobian).eval().format(IPythonFormat));
        d.reducedJ.noalias() += explicit_.outDers().rviewTranspose(d.jacobian).eval() * Je_;
        hppDnum (info, "Jacobian of stack " << i << " after update: \n" << d.reducedJ.format(IPythonFormat));
      }
    }

    void HybridSolver::projectOnKernel (vectorIn_t arg, vectorIn_t darg, vectorOut_t result) const
    {
      computeValue<true> (arg);
      updateJacobian(arg);
      getReducedJacobian (reducedJ_);

      svd_.compute (reducedJ_);

      dqSmall_ = reduction_.rviewTranspose(darg);

      vector_t tmp = getV1(svd_).adjoint() * dqSmall_;
      dqSmall_.noalias() -= getV1(svd_) * tmp;

      reduction_.lviewTranspose(result) = dqSmall_;
    }

    template HybridSolver::Status HybridSolver::impl_solve (vectorOut_t arg, lineSearch::Backtracking   lineSearch) const;
    template HybridSolver::Status HybridSolver::impl_solve (vectorOut_t arg, lineSearch::FixedSequence  lineSearch) const;
    template HybridSolver::Status HybridSolver::impl_solve (vectorOut_t arg, lineSearch::ErrorNormBased lineSearch) const;
  } // namespace constraints
} // namespace hpp