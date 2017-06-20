#include <Rcpp.h>
using namespace Rcpp;
#include <iostream>
#include <vector>
#include <algorithm>
#include <ctime>
#include <string>

#include "optimize_lambda1.hpp"
#include "util.hpp"
#include "graph_trails.hpp" //boost_build_patch_graph_components
#include <boost/math/tools/minima.hpp> //brent_find_minima

obj_lambda1::obj_lambda1(double minval, double maxval, double tol_val,
                         IntegerVector patchno, NumericVector forbidden_vals,
                         NumericVector value, NumericVector weight, NumericVector valuehat,
                         NumericVector ncounts) : minval_(minval), maxval_(maxval),
    valrange_(maxval-minval),
    tol_val_(tol_val), lsnc_(log(sum(ncounts))),
    patchno_(patchno), forbidden_vals_(forbidden_vals),
    value_(value), weight_(weight), valuehat_(valuehat) {}

double obj_lambda1::operator()(double x) const {
    //return get(std::pow(10,x), "opt")["BIC"];
    return get(std::pow(10,x))["BIC"];
}

NumericVector obj_lambda1::get(double lambda1, std::string msg) const {
    const bool constrained = true;
    if (constrained) {
        if (is_true(any(abs(forbidden_vals_)>lambda1+tol_val_/2))) {
            if (!msg.empty()) Rcout << " OBJ " << msg << " forbidden lambda1= " << lambda1
                                    << " eCprime= 0 BIC= Inf dof= NA" << std::endl;
            return NumericVector::create(_["eCprime"]=0, _["lambda1"]=lambda1,
                                         _["BIC"]=std::numeric_limits<double>::max(), _["dof"]=NumericVector::get_na());
        }
    }
    std::vector<double> value_r = as<std::vector<double> >(value_);
    NumericVector soft = wrap(soft_threshold(value_r, 0, lambda1));
    IntegerVector selected = patchno_[abs(soft)>tol_val_/2];
    const int dof = unique(selected).size();
    const double BIC = sum(weight_ * SQUARE(valuehat_ - soft)) + lsnc_*dof;
    if (!msg.empty()) Rcout << " OBJ " << msg << " ok lambda1= " << lambda1 << " eCprime= 0"
                            << " BIC= " << BIC  << " dof= " << dof << std::endl;
    return NumericVector::create(_["eCprime"]=0, _["lambda1"]=lambda1, _["BIC"]=BIC,
                                 _["dof"]=dof);
}

NumericVector refine_minimum(const obj_lambda1& obj, double lam1,
                             double lam1_min, int refine_num, NumericVector patchvals, bool positive) {
    //bounds and border case
    NumericVector lambdavals;
    if (positive) {
        lambdavals = patchvals[patchvals>=0];
    } else {
        lambdavals = abs(patchvals);
    }
    lambdavals = lambdavals[lambdavals>=lam1_min];
    if (lambdavals.size() == 0) {
        Rcout << " warning: lambda1 minimum was found in an interior point" << std::endl;
        //return obj.get(lam1, "refine");
        return obj.get(lam1);
    }
    NumericVector best, val;
    //evaluate at lower bound, even if there's no patch value
    //best = obj.get(lam1_min, "refine");
    best = obj.get(lam1_min);
    //values < lambda1
    NumericVector candidates1 = lambdavals[lambdavals<lam1];
    int nc1 = candidates1.size();
    for (int i=0; i<std::min(nc1,refine_num); ++i) {
        //val = obj.get(candidates1[nc1-1-i], "refine");
        val = obj.get(candidates1[nc1-1-i]);
        if (as<double>(val["BIC"]) < as<double>(best["BIC"])) best=val;
    }
    //values >= lambda1
    NumericVector candidates2 = lambdavals[lambdavals>=lam1];
    int nc2 = candidates2.size();
    for (int i=0; i<std::min(nc2,refine_num); ++i) {
        //val = obj.get(candidates2[i], "refine");
        val = obj.get(candidates2[i]);
        if (as<double>(val["BIC"]) < as<double>(best["BIC"])) best=val;
    }
    return(best);
}

NumericVector cpp_optimize_lambda1(const DataFrame mat, int nbins,
                                   double tol_val, bool positive,
                                   double lambda1_min, int refine_num) {
    const bool constrained = true;
    //extract vectors
    double lmin = std::max(lambda1_min,tol_val/2);
    std::clock_t c_start = std::clock();
    NumericVector weight = mat["weight"];
    NumericVector phihat = mat["phihat"];
    NumericVector beta = mat["beta"];
    NumericVector ncounts = mat["ncounts"];
    IntegerVector diag_idx = mat["diag.idx"];
    //get patch nos and sorted values
    List cl = boost_build_patch_graph_components(nbins, mat, tol_val);
    IntegerVector patchno = cl["membership"];
    NumericVector patchvals = get_patch_values(beta, patchno);
    //treat border case, assuming constraint is on
    if (as<double>(cl["no"]) == 1) {
        /*Rcout << " OBJ final ok lambda1= " << max(abs(patchvals))
                << " eCprime= 0 BIC= " << sum(weight * SQUARE(phihat)) << " dof= 0" << std::endl;*/
        return NumericVector::create(_["eCprime"]=0,
                                     _["lambda1"]=max(abs(patchvals)), _["dof"]=0,
                                     _["BIC"]=sum(weight * SQUARE(phihat)),
                                     _["c_init"]=-1, _["c_brent"]=-1, _["c_refine"]=-1);
    }
    double minval = patchvals(0);
    double maxval = patchvals(patchvals.size()-1);
    double lmax = std::max(std::abs(maxval),std::abs(minval));
    //since constraint is on, decay and signal must adjust so that
    //there is at least one zero signal value per diagonal idx
    NumericVector forbidden_vals;
    if (constrained) {
        if (positive) {
            forbidden_vals = get_minimum_diagonal_values(beta, diag_idx);
            lmin = std::max(lmin, std::max(std::abs(forbidden_vals(0)), std::abs(forbidden_vals(forbidden_vals.size()-1))));
        } else {
            forbidden_vals = get_constant_diagonal_values(beta, diag_idx, tol_val);
        }
    }
    /*Rcout << "lmin= " << lmin << " valrange= " << (maxval-minval) << std::endl;
    for (int i=0; i<forbidden_vals.size(); ++i) Rcout << "fv[ " << i << " ]= "<< forbidden_vals[i] << std::endl;*/
    //create functor
    obj_lambda1 obj(minval, maxval, tol_val, patchno, forbidden_vals,
                    beta, weight, phihat, ncounts);
    std::clock_t c_in1 = std::clock();
    //treat second border case
    if (lmin>lmax) {
        //NumericVector retval = obj.get(lmin, "final");
        NumericVector retval = obj.get(lmin);
        return NumericVector::create(_["eCprime"]=0, _["lambda1"]=retval["lambda1"],
                                     _["BIC"]=retval["BIC"], _["dof"]=retval["dof"],
                                     _["c_init"]=c_in1-c_start, _["c_brent"]=-1, _["c_refine"]=-1);
    }
    //grid
    //for (int i=0; i<1000; ++i) obj.get(1+i/999.*5, "grid");
    //optimize
    int bits = -8*std::log10(tol_val)+1;
    boost::uintmax_t maxiter = 1000;
    std::pair<double,double> ret = boost::math::tools::brent_find_minima(obj,
                                   std::log10(lmin), std::log10(lmax), bits, maxiter);
    //now compute the minimum among the n closest candidates (brent can get stuck in local minima)
    std::clock_t c_in2 = std::clock();
    double lam1=pow(10,ret.first);
    //obj.get(lam1,"minimum");
    NumericVector retval = refine_minimum(obj, lam1, lmin, refine_num, patchvals,
                                          positive);
    std::clock_t c_in3 = std::clock();
    //obj.get(as<double>(retval["lambda1"]),"final");
    return NumericVector::create(_["eCprime"]=retval["eCprime"],
                                 _["lambda1"]=retval["lambda1"],
                                 _["BIC"]=retval["BIC"], _["dof"]=retval["dof"],
                                 _["c_init"]=c_in1-c_start, _["c_brent"]=c_in2-c_in1, _["c_refine"]=c_in3-c_in2);
}
