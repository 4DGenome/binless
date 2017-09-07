#include <Rcpp.h>
using namespace Rcpp;
#include <iostream>
#include <vector>
#include <algorithm>
#include "util.hpp"

std::vector<double> soft_threshold(const std::vector<double>& beta,
                                   double eCprime, double lam1) {
    std::vector<double> phi;
    phi.reserve(beta.size());
    for (std::vector<double>::const_iterator it = beta.begin(); it != beta.end();
            ++it) {
        double val = *it - eCprime;
        phi.push_back((val > 0) ? std::max(0., val - lam1) : std::min(0., val + lam1));
    }
    return phi;
}

//return ascending list of the values of all detected patches in the matrix
NumericVector get_patch_values(NumericVector value, IntegerVector patchno) {
    int npatches = max(patchno)+1;
    NumericVector unique_values(npatches); //patchno starts at 0
    for (int i=0; i<patchno.size(); ++i) unique_values(patchno(i)) = value(i);
    std::sort(unique_values.begin(), unique_values.end());
    return unique_values;
}

Rcpp::NumericVector compute_phi_ref(const BinnedData<Difference>& binned,
                                    const Rcpp::NumericVector& delta) {
  const int N = delta.size();
  std::vector<double> phihat = Rcpp::as<std::vector<double> >(binned.get_phihat());
  std::vector<double> phihat_var = Rcpp::as<std::vector<double> >(binned.get_phihat_var());
  std::vector<double> phihat_ref = Rcpp::as<std::vector<double> >(binned.get_phihat_ref());
  std::vector<double> phihat_var_ref = Rcpp::as<std::vector<double> >(binned.get_phihat_var_ref());
  std::vector<double> phi_ref_r;
  phi_ref_r.reserve(N);
  for (int i=0; i<N; ++i) {
    double val;
    if (phihat_var_ref[i]==INFINITY && phihat_var[i]==INFINITY) {
      val=(phihat_ref[i]+phihat[i])/2;
    } else {
      val=(phihat_ref[i]/phihat_var_ref[i] + (phihat[i]-delta[i])/phihat_var[i])
      /(1/phihat_var_ref[i]+1/phihat_var[i]);
    }
    val = std::max(val,0.);
    phi_ref_r.push_back(val);
  }
  return Rcpp::wrap(phi_ref_r);
}

Rcpp::NumericVector get_forbidden_values(const BinnedDataCore& binned) {
    Rcpp::NumericVector beta = binned.get_beta();
    Rcpp::IntegerVector diag_grp = binned.get_diag_grp();
    //retrieve minimum values for each counter diagonal group
    //TODO: abs(beta) or beta?
    int ndiags = max(diag_grp)+1;
    Rcpp::LogicalVector diagtouch(ndiags, false);
    Rcpp::NumericVector diagvals(ndiags, max(beta)); //diag_idx starts at 0
    for (int i=0; i<diag_grp.size(); ++i) {
        diagvals(diag_grp(i)) = std::min(diagvals(diag_grp(i)),beta(i));
        diagtouch(diag_grp(i)) = true;
    }
    diagvals = diagvals[diagtouch];
    diagvals = unique(diagvals);
    std::sort(diagvals.begin(), diagvals.end());
    return diagvals;
}
