#ifndef SPARSITY_ESTIMATOR_HPP
#define SPARSITY_ESTIMATOR_HPP

#include <Rcpp.h>
#include <vector>

#include "CandidatesFilter.hpp"
#include "BoundsComputer.hpp"
#include "BoundsChecker.hpp"
#include "ScoreComputer.hpp"
#include "ScoreOptimizer.hpp"
#include "util.hpp"
#include "BinnedData.hpp"

// A class that estimates lambda1 (and an offset if needed) based on various criteria
/* We define and use upper bound UB = offset + lambda1 and lower bound LB = offset - lambda1
 * BIC and CV have the same quadratic functional form when no patch is added/removed.
 * When UB and LB vary between two patches (or outside any patch), the quadratic has only one minimum
 * In this setting, when UB is chosen, positivity/offset constraints imply the correct LB
 * We thus generate a list of upper bound candidates that get optimized between two patches
 * and evaluate the BIC or the CV at these optimized values
 */
template<typename Calculation, //whether it's a Signal or a Difference calculation
         typename Score, //to choose between BIC, CV or CVkSD
         typename Offset, //whether offset should be held at zero (ZeroOffset) or estimated (EstimatedOffset)
         typename Sign, //whether there is no constraint on the sign of the estimate (AnySign) or whether it must be positive (PositiveSign)
         typename Degeneracy = ForbidDegeneracy> //whether to forbid certain values in order to avoid degeneracies in the model
class SparsityEstimator : private CandidatesFilter<Degeneracy>,
                          private BoundsComputer<Offset,Sign>,
                          private BoundsChecker<Sign>,
                          private BoundsChecker<Degeneracy>,
                          private ScoreComputer<Calculation,Score>,
                          private ScoreOptimizer<Score> {
    
    typedef typename ScoreComputer<Calculation,Score>::value_t score_t;
    typedef BinnedData<Calculation> binned_t;
    typedef typename ScoreComputer<Calculation,Score>::likelihood_var_t likelihood_var_t;
    typedef typename ScoreComputer<Calculation,Score>::assembler_var_t assembler_var_t;

public:
    
    SparsityEstimator(int nbins, double tol_val, const binned_t& binned, double lambda2,
                      const likelihood_var_t& likelihood_var,
                      const assembler_var_t& assembler_var) :
     CandidatesFilter<Degeneracy>(binned),
     BoundsComputer<Offset,Sign>(binned, min(binned.get_beta())),
     BoundsChecker<Sign>(binned),
     BoundsChecker<Degeneracy>(binned),
     ScoreComputer<Calculation,Score>(tol_val, binned, likelihood_var, assembler_var),
     ScoreOptimizer<Score>(),
     lambda2_(lambda2), UBcandidates_(get_UB_candidates(nbins, tol_val, binned)) {}
    
     score_t optimize() const;
    
private:
    
    Rcpp::NumericVector expand_values(const NumericVector& values) const;

    //propose a set of candidates for an upper bound based on a list of patch borders
    Rcpp::NumericVector candidates_from_borders(const Rcpp::NumericVector& borders) const;

    Rcpp::NumericVector get_UB_candidates(int nbins, double tol_val, const binned_t& binned) const;

    const double lambda2_;
    const Rcpp::NumericVector UBcandidates_;

};

#include "SparsityEstimator.ipp"


#endif

