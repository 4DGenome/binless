#ifndef CANDIDATES_GENERATOR_HPP
#define CANDIDATES_GENERATOR_HPP

#include <Rcpp.h>
#include <vector>
#include <algorithm>

#include "CandidatesValues.hpp"
#include "BinnedData.hpp"
#include "minimum_UB.hpp"

Rcpp::NumericVector get_patch_values(const Rcpp::NumericVector& value, const Rcpp::IntegerVector& patchno);
Rcpp::NumericVector borders_from_values(int nbins, double tol_val, const BinnedDataCore& binned,
                                        const Rcpp::NumericVector& values);

Rcpp::NumericVector cleanup_borders(const Rcpp::NumericVector& borders, double minUB);

//propose a set of candidates for an upper bound based on a list of patch borders
Rcpp::NumericVector candidates_from_borders(const Rcpp::NumericVector& borders);



template<typename Offset, //whether offset should be held at zero (ZeroOffset) or estimated (EstimatedOffset)
         typename Sign, //whether there is no constraint on the sign of the estimate (AnySign) or whether it must be positive (PositiveSign)
         typename Degeneracy> //whether to forbid certain values (ForbidDegeneracy) in order to avoid degeneracies in the model, or not (AllowDegeneracy)
class CandidatesGenerator : private CandidatesValues<Offset,Sign> {
public:
    
    CandidatesGenerator(int nbins, double tol_val, const BinnedDataCore& binned)
      : CandidatesValues<Offset,Sign>(binned),
        UBcandidates_(generate(nbins, tol_val, binned)) {}

    Rcpp::NumericVector get_UB_candidates() const { return UBcandidates_; }

private:
    Rcpp::NumericVector generate(int nbins, double tol_val, const BinnedDataCore& binned) const {
        //get minimum admissible UB
        double minUB = get_minimum_UB<Offset,Sign,Degeneracy>(binned);
        //get UB borders: values at which dof changes (including zero weight)
        Rcpp::NumericVector values = CandidatesValues<Offset,Sign>::get();
        Rcpp::NumericVector borders = borders_from_values(nbins,tol_val, binned, values);
        //cleanup the borders list, adding minUB as the lowest possible, expanding at the end
        borders = cleanup_borders(borders, minUB);
        //now generate candidates from these borders
        //pick center of each interval to make it an UB candidate
        Rcpp::NumericVector UBcandidates = candidates_from_borders(borders);
        return UBcandidates;
    }
   
    const Rcpp::NumericVector UBcandidates_;
};

#endif

