#include <Rcpp.h>
using namespace Rcpp;
#include <iostream>
#include <vector>
#include <algorithm>
#include <Eigen/Dense>
#include <Eigen/Sparse>

#include "fast_decay.hpp"
#include "fast_residuals.hpp"
#include "spline.hpp"
#include "gam.hpp"
#include "fast_expected.hpp"

namespace binless {
namespace fast {


//here, log decay is log ( sum_i observed / sum_i expected ) with i summed over counter diagonals
DecaySummary compute_poisson_lsq_log_decay(const FastSignalData& data, const DecayEstimate& dec) {
  //get observed and expected data
  Eigen::VectorXd sum_obs = Eigen::VectorXd::Zero(data.get_nbins());
  Eigen::VectorXd sum_exp = Eigen::VectorXd::Zero(data.get_nbins());
  Eigen::VectorXd distance = Eigen::VectorXd::Zero(data.get_nbins());
  Eigen::VectorXd ncounts = Eigen::VectorXd::Zero(data.get_nbins());
  auto log_expected = get_log_expected(data, dec);
  auto observed = data.get_observed();
  //sum them along the counter diagonals
  auto dbin1 = data.get_bin1();
  auto dbin2 = data.get_bin2();
  auto ddistance = data.get_distance();
  for (unsigned i=0; i<data.get_N(); ++i) {
    unsigned dist = dbin2[i]-dbin1[i];
    double expected = std::exp(log_expected[i]);
    sum_obs(dist) += observed[i];
    sum_exp(dist) += expected;
    distance(dist) += ddistance[i];
    ncounts(dist) += 1;
  }
  distance = (distance.array() / ncounts.array()).matrix();
  //compute log_decay
  Eigen::VectorXd log_decay = Eigen::VectorXd::Zero(data.get_nbins());
  for (unsigned i=0; i<data.get_nbins(); ++i) {
    if (sum_obs(i)==0) {
      Rcpp::Rcout << "counter diag " << i << " is zero!\n";
      Rcpp::stop(" Aborting...");
    }
  }
  log_decay = (sum_obs.array()/sum_exp.array()).log().matrix();
  //center log_decay
  double avg = log_decay.sum()/log_decay.rows();
  log_decay = (log_decay.array() - avg).matrix();
  return DecaySummary{distance,log_decay,log_decay.array().exp().matrix(),ncounts};
}

DecaySummary get_decay_summary(const FastSignalData& data, const DecayEstimate& dec) {
  //get residuals
  ResidualsPair z = get_poisson_residuals(data, dec);
  //sum them along the diagonals
  auto dbin1 = data.get_bin1();
  auto dbin2 = data.get_bin2();
  auto ddist = data.get_distance();
  Eigen::VectorXd kappahat = Eigen::VectorXd::Zero(data.get_nbins());
  Eigen::VectorXd weight = Eigen::VectorXd::Zero(data.get_nbins());
  Eigen::VectorXd distance = Eigen::VectorXd::Zero(data.get_nbins());
  Eigen::VectorXd ncounts = Eigen::VectorXd::Zero(data.get_nbins());
  for (unsigned i=0; i<data.get_N(); ++i) {
    unsigned bin1 = dbin1[i]-1; //offset by 1 for vector indexing
    unsigned bin2 = dbin2[i]-1;
    kappahat(bin2-bin1) += z.residuals[i]*z.weights[i];
    weight(bin2-bin1) += z.weights[i];
    distance(bin2-bin1) += ddist[i];
    ncounts(bin2-bin1) += 1;
  }
  distance = (distance.array() / ncounts.array()).matrix();
  //add current bias and normalize
  for (unsigned i=0; i<data.get_nbins(); ++i) {
    kappahat(i) = (weight(i)>0) ? kappahat(i)/weight(i) : 0;
  }
  auto log_decay = dec.get_log_decay(distance.array().log().matrix());
  kappahat += log_decay;
  return DecaySummary{distance,kappahat,weight,ncounts};
}

void spline_log_decay_fit(const DecaySummary& summary, DecayEstimate& dec, double tol_val, const DecaySchedule& schedule) {
  //extract data
  const Eigen::VectorXd y(summary.kappahat);
  const Eigen::VectorXd w(summary.weight);
  const Eigen::VectorXd S = w.array().inverse().sqrt().matrix();
  //X: build spline base on log distance
  const Eigen::ArrayXd distance(summary.distance.array());
  const Eigen::VectorXd log_distance = distance.log().matrix();
  const Eigen::SparseMatrix<double> X = dec.get_design(log_distance);
  //D: build difference matrix
  const Eigen::SparseMatrix<double> D = second_order_difference_matrix(schedule.Kdiag);
  //C: build constraint matrix to forbid increase
  const Eigen::SparseMatrix<double> Cin = - first_order_difference_matrix(schedule.Kdiag);
  
  //iteratively fit GAM on decay and estimate lambda
  GeneralizedAdditiveModel gam(y,S,X,D,schedule.sigma);
  gam.set_inequality_constraints(Cin);
  gam.optimize(schedule.max_iter,tol_val);
  //Rcpp::Rcout << "gam converged: " << gam.has_converged() << "\n";
  dec.set_beta_diag(gam.get_beta());
  dec.center_log_decay(log_distance, summary.ncounts);
  dec.set_lambda_diag(gam.get_lambda());
  dec.set_summary(summary);
  /*Rcpp::Rcout << "spline_log_decay_fit\n";
  Rcpp::Rcout << "distance kappahat weight ncounts log_decay\n";
  Rcpp::Rcout << (Eigen::MatrixXd(summary.distance.rows(),5) << summary.distance, summary.kappahat,
                  summary.weight, summary.ncounts, dec.get_log_decay(summary.distance.array().log().matrix())).finished();*/
}

//one IRLS iteration for log decay, with a poisson model
void step_log_decay(const FastSignalData& data, DecayEstimate& dec, double tol_val) {
  //compute summary statistics for decay
  DecaySchedule schedule;
  DecaySummary summary = get_decay_summary(data, dec);
  //infer new decay from summaries
  spline_log_decay_fit(summary, dec, tol_val, schedule);
}

}
}

