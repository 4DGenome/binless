#ifndef FUSED_LASSO_GENERALIZED_ESTIMATOR_HPP
#define FUSED_LASSO_GENERALIZED_ESTIMATOR_HPP

#include <Rcpp.h>
using namespace Rcpp;
#include <vector>

#include "util.hpp"
#include "FusedLassoGaussianEstimator.hpp"

// A class that computes the 2D triangle grid fused lasso solution on some data.
// This class assumes that weights can change and uses an underlying gaussian model in an iterative way (IRLS).
template<typename Library, typename WeightUpdater>
class FusedLassoGeneralizedEstimator : public FusedLassoGaussianEstimator<Library>, public WeightUpdater {
    
public:
    
    //initialize the problem with a triangle grid with nrows
    //requesting precision to be below a given convergence criterion
    //final beta value will be clamped if clamp>0
    FusedLassoGeneralizedEstimator(unsigned nrows, double converge, double clamp=-1)
    : FusedLassoGaussianEstimator<Library>(nrows, converge, clamp), WeightUpdater() {}
    
    //run the optimization on the given data. The objective is
    // sum_i w_i(y_i-beta_i)^2 + lambda2 * sum_ij |beta_i-beta_j|
    // y, w and lambda2 are held constant, while beta starts at beta_init
    void optimize(const std::vector<double>& y, const std::vector<double>& beta_init,
                  const std::vector<double>& w, double lambda2) {
        Library::prepare(beta_init);
        Library::optimize(y, w, lambda2);
    }
    
    //return soft-thresholded value, corresponding to the problem
    // sum_i w_i(y_i-offset-beta_i)^2 + lambda2 * sum_ij |beta_i-beta_j| + lambda1 * sum_i |beta_i|
    //where the solution beta for a given lambda2 was already computed
    //values will be clamped if necessary
    //returns an empty vector if optimize has not been called
    std::vector<double> get(double offset=0., double lambda1=0.) const {
        return soft_threshold(clamp(Library::get_beta()), offset, lambda1);
    }
    
private:
    std::vector<double> clamp(std::vector<double> beta) const {
        //clamp values at +- clamp_ if needed
        if (clamp_>0) {
            for (double& i : beta) {
                i = std::min(clamp_, std::max(-clamp_, i));
            }
        }
        return beta;
    }
    
    const double clamp_;
    
};


#endif

