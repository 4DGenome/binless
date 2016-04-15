////
// Cut-site normalization model: fit available data
////
functions {
  ///////////
  //BEGIN internal use
  //
  //generate [imin, imin+1, ... imax]
  vector range(int imin, int imax) {
    return cumulative_sum(rep_vector(1,imax-imin+1))-1+imin;
  }
  //Compute cumulative histogram of x at left cut points q
  //i.e. index of first x value that falls in bins defined by cutpoints defined in q
  //assumes x is ordered.
  //x[hist[i]] is the first value in x that is >= q[i]
  int[] cumulative_hist(vector x, row_vector q) {
    int indices[cols(q)];
    int ix;
    int N;
    ix <- 1;
    N <- rows(x);
    for (iq in 1:cols(q)) {
      while (ix < N && x[ix] < q[iq]) ix <- ix + 1;
      indices[iq] <- ix;
    }
    return indices;
  }
  matrix bspl_gen(vector x, real dx, row_vector t, int q) {
    int N;
    int K;
    K <- cols(t);
    N <- rows(x);
    {
      int r[K];
      matrix[N, K] T;
      matrix[N, K] X;
      matrix[N, K] P;
      matrix[N, K] B;
      for (i in 2:K) r[i-1] <- i;
      r[K] <- 1;
      T <- rep_matrix(t, N);
      X <- rep_matrix(x, K);
      P <- (X - T) / dx;
      for (i in 1:N)
        for (j in 1:K)
          B[i,j] <- (T[i,j] <= X[i,j]) && (X[i,j] < T[i,j]+dx);
      for (k in 1:q)
        B <- ( P .* B + (k+1-P) .* B[,r]) / k;
      return B;
    }
  }
  //END internal use
  ///////////
  
  int splinedegree() {return 3;} //set 3 for cubic spline
  
  int nnz(int N) {return N*(splinedegree()+1);} //nonzero count for design matrix
  
  row_vector column_sums(int K, vector Xw, int[] Xv) {
    row_vector[K] sums;
    sums <- rep_row_vector(0,K);
    for (i in 1:rows(Xw)) {
      sums[Xv[i]] <- sums[Xv[i]] + Xw[i];
    }
    return sums;
  }
  
  matrix bspline(vector x, int K, int q) {
    real dx; //interval length
    row_vector[K] t; //knot locations (except last)
    //
    dx <- 1.01*(max(x)-min(x))/(K-q); //make it slightly larger
    t <- min(x) - dx*0.01 + dx * range(-q,K-q-1)';
    return bspl_gen(x, dx, t, q);
  }

  real neg_binomial_2_log_deviance(int[] y, vector log_mu, real alpha) {
    vector[size(y)] y_vec;
    vector[size(y)] y_mod;
    vector[rows(log_mu)] mu;
    if (rows(log_mu) != size(y)) reject("sizes of y (",size(y),") and log_mu (", rows(log_mu),
                                        ") must match")
    y_vec <- to_vector(y);
    for (i in 1:size(y)) if (y[i]>0) {y_mod[i] <- y[i];} else {y_mod[i] <-1;}
    mu <- exp(log_mu);
    return 2*sum( (y_vec+alpha) .* log( (mu+alpha) ./ (y_vec+alpha) )
                  + y_vec .* log(y_mod ./ mu));
  }
}
////////////////////////////////////////////////////////////////
data {
  //spline parameters
  int Krow; //number of functions in spline base for row biases
  int Kdiag; //number of functions in spline base for diagonal decay
  //biases
  int<lower=1> S; //number of cut sites
  vector[S] cutsites; //cut site locations
  int rejoined[S];
  int danglingL[S];
  int danglingR[S];
  //counts
  int<lower=1> N; //number of data points
  int<lower=0> counts[4,N]; //raw counts: Close, Far, Up, Down
  int<lower=1,upper=S> cidx[2,N]; //index of its associated cut site
  //fixed parameters
  real<lower=0> lambda_nu;
  real<lower=0> lambda_delta;
  real<lower=0> lambda_diag;
}
transformed data {
  //bias spline, sparse (nu and delta have the same design)
  vector[nnz(S)] Xrow_w;
  int Xrow_v[nnz(S)];
  int Xrow_u[S+1];
  vector[Krow] prow;
  //diagonal SCAM spline, dense
  matrix[N,Kdiag] Xdiag;
  row_vector[Kdiag-1] pdiag;
  
  ////bias spline, sparse (nu and delta have the same design)
  //BEGIN sparse calculation
  //cannot write function that modifies its arguments so we put it here
  //input: vector[N] cutsites, int Krow, int splinedegree()
  //output: vector[nnz(S)] Xrow_w, int Xrow_v[nnz(S)], int Xrow_u[S+1]
  {
    real dx; //interval length
    row_vector[Krow] t; //Krownot locations (except last)
    int x_n[Krow-splinedegree()+1]; //cumulative histogram of cutsites values
    int idx_u; //counters for filling of sparse matrix
    int idx_w;
    //
    dx <- 1.01*(max(cutsites)-min(cutsites))/(Krow-splinedegree()); //maKrowe it slightly larger
    t <- min(cutsites) - dx*0.01 + dx * range(-splinedegree(),Krow-splinedegree()-1)';
    //get indices of cutsites which cross to the next segment and build x_n.
    x_n[1] <- 1;
    x_n[2:(Krow-splinedegree())] <- cumulative_hist(cutsites, t[(splinedegree()+2):]);
    x_n[Krow-splinedegree()+1] <- rows(cutsites)+1;
    //
    //build spline, interval per interval
    idx_u <- 1;
    idx_w <- 1;
    for (i in 1:(Krow-splinedegree())) {
      //at any cutsites, there are splinedegree()+1 nonzero b-splines. Compute them.
      int xbegin;
      int xend;
      xbegin <- x_n[i];
      xend <- x_n[i+1]-1;
      {
        matrix[xend-xbegin+1,splinedegree()+1] tmp;
        tmp <- bspl_gen(cutsites[xbegin:xend], dx, t[i:(i+splinedegree())], splinedegree());
        for (ti in 1:(xend-xbegin+1)) {
          Xrow_u[idx_u] <- idx_w;
          idx_u <- idx_u + 1;
          for (tj in 1:(splinedegree()+1)) {
            Xrow_w[idx_w] <- tmp[ti,tj];
            Xrow_v[idx_w] <- i+tj-1;
            idx_w <- idx_w + 1;
          }
        }
      }
    }
    Xrow_u[idx_u] <- idx_w; 
  }
  //END sparse calculation

  //diagonal SCAM spline, dense
  {
    //can't do abs() on a vector so be inventive
    vector[N] tmp;
    tmp <- cutsites[cidx[2]]-cutsites[cidx[1]];
    Xdiag <- bspline(0.5*log(tmp .* tmp), Kdiag, splinedegree());
  }
  
  
  //projector for biases (GAM)
  prow <- column_sums(Krow, Xrow_w, Xrow_v)';
  prow <- prow / sqrt(dot_self(prow));
  //projector for diagonal (SCAM)
  {
    row_vector[Kdiag] tmp;
    tmp <- rep_row_vector(1,N) * Xdiag;
    pdiag <- -tmp[2:] / (tmp * rep_vector(1,Kdiag));
  }
}
parameters {
  real eC;  //exposure for counts
  real eRJ; //exposure for rejoined ends
  real eDL; //exposure for dangling ends
  real eDR; //exposure for dangling ends
  vector[Krow-1] beta_nu;
  vector[Krow-1] beta_DL;
  vector[Krow-1] beta_DR;
  positive_ordered[Kdiag-1] beta_diag;
  real<lower=0> alpha;
}
transformed parameters {
  //nu
  vector[S] log_nu; // log(nu)
  vector[Krow-2] beta_nu_diff; //2nd order difference on beta_nu_aug
  vector[N] log_nui;
  vector[N] log_nuj;
  //DL
  vector[S] log_DL; // log(DL)
  vector[Krow-2] beta_DL_diff; //2nd order difference on beta_DL_aug
  vector[N] log_DLi;
  vector[N] log_DLj;
  //DR
  vector[S] log_DR; // log(DR)
  vector[Krow-2] beta_DR_diff; //2nd order difference on beta_DR_aug
  vector[N] log_DRi;
  vector[N] log_DRj;
  //diag
  vector[N] log_decay;
  vector[Kdiag-2] beta_diag_diff;
  //means
  vector[S] log_mean_DL;
  vector[S] log_mean_DR;
  vector[S] log_mean_RJ;
  vector[N] base_count;
  //
  vector[N] log_mean_cup;
  vector[N] log_mean_cdown;
  vector[N] log_mean_cfar;
  vector[N] log_mean_cclose;
  
  //nu
  {
    vector[Krow] beta_nu_centered;
    vector[Krow] beta_nu_aug;
    beta_nu_aug[1] <- sum(beta_nu);
    beta_nu_aug[2:] <- beta_nu;
    beta_nu_centered <- beta_nu_aug - (beta_nu_aug' * prow) * prow;
    log_nu <- csr_matrix_times_vector(S, Krow, Xrow_w, Xrow_v, Xrow_u, beta_nu_centered);
    beta_nu_diff <- beta_nu_aug[:(Krow-2)]-2*beta_nu_aug[2:(Krow-1)]+beta_nu_aug[3:];
  }
  log_nui <- log_nu[cidx[1]];
  log_nuj <- log_nu[cidx[2]];
  //DL
  {
    vector[Krow] beta_DL_centered;
    vector[Krow] beta_DL_aug;
    beta_DL_aug[1] <- sum(beta_DL);
    beta_DL_aug[2:] <- beta_DL;
    beta_DL_centered <- beta_DL_aug - (beta_DL_aug' * prow) * prow;
    log_DL <- csr_matrix_times_vector(S, Krow, Xrow_w, Xrow_v, Xrow_u, beta_DL_centered);
    beta_DL_diff <- beta_DL_aug[:(Krow-2)]-2*beta_DL_aug[2:(Krow-1)]+beta_DL_aug[3:];
  }
  log_DLi <- log_DL[cidx[1]];
  log_DLj <- log_DL[cidx[2]];
  //DR
  {
    vector[Krow] beta_DR_centered;
    vector[Krow] beta_DR_aug;
    beta_DR_aug[1] <- sum(beta_DR);
    beta_DR_aug[2:] <- beta_DR;
    beta_DR_centered <- beta_DR_aug - (beta_DR_aug' * prow) * prow;
    log_DR <- csr_matrix_times_vector(S, Krow, Xrow_w, Xrow_v, Xrow_u, beta_DR_centered);
    beta_DR_diff <- beta_DR_aug[:(Krow-2)]-2*beta_DR_aug[2:(Krow-1)]+beta_DR_aug[3:];
  }
  log_DRi <- log_DR[cidx[1]];
  log_DRj <- log_DR[cidx[2]];
  //decay
  {
    vector[Kdiag] beta_diag_centered;
    real epsilon;
    real val;
    epsilon <- -1; //+1 for increasing, -1 for decreasing spline
    val <- epsilon*pdiag*beta_diag;
    beta_diag_centered[1] <- val;
    beta_diag_centered[2:] <- val+epsilon*beta_diag;
    log_decay <- Xdiag * beta_diag_centered;
    beta_diag_diff <- beta_diag_centered[:(Kdiag-2)]-2*beta_diag_centered[2:(Kdiag-1)]+beta_diag_centered[3:];
  }

  //means
  {
    log_mean_RJ <- log_nu + eRJ - log_DL - log_DR;
    log_mean_DL <- log_nu + eDL + log_DL;
    log_mean_DR <- log_nu + eDR + log_DR;
    //
    base_count <- eC + log_decay + log_nui + log_nuj;
    log_mean_cclose <- base_count - log_DLi - log_DRj;
    log_mean_cfar   <- base_count - log_DRi - log_DLj;
    log_mean_cup    <- base_count - log_DRi - log_DRj;
    log_mean_cdown  <- base_count - log_DLi - log_DLj;
  }
}
model {
  //// likelihoods
  //biases
  rejoined ~ neg_binomial_2_log(log_mean_RJ, alpha);
  danglingL ~ neg_binomial_2_log(log_mean_DL, alpha);
  danglingR ~ neg_binomial_2_log(log_mean_DR, alpha);
  //counts: Close, Far, Up, Down
  counts[1] ~ neg_binomial_2_log(log_mean_cclose, alpha); // Close
  counts[2] ~ neg_binomial_2_log(log_mean_cfar, alpha); // Far
  counts[3] ~ neg_binomial_2_log(log_mean_cup, alpha); // Up
  counts[4] ~ neg_binomial_2_log(log_mean_cdown, alpha); // Down
  
  //// Priors
  //P-spline prior on the differences (K-2 params)
  //warning on jacobian can be ignored
  //see GAM, Wood (2006), section 4.8.2 (p.187)
  beta_nu_diff ~ normal(0, 1./(alpha*lambda_nu));
  beta_DL_diff ~ normal(0, 1./(alpha*lambda_delta));
  beta_DR_diff ~ normal(0, 1./(alpha*lambda_delta));
  beta_diag_diff ~ normal(0, 1./(alpha*lambda_diag));
}
generated quantities {
  real deviance;
  real deviance_null;
  real deviance_proportion_explained;
  deviance <- neg_binomial_2_log_deviance(rejoined, log_mean_RJ, alpha) +
              neg_binomial_2_log_deviance(danglingL, log_mean_DL, alpha) +
              neg_binomial_2_log_deviance(danglingR, log_mean_DR, alpha) +
              neg_binomial_2_log_deviance(counts[1], log_mean_cclose, alpha) +
              neg_binomial_2_log_deviance(counts[2], log_mean_cfar, alpha) +
              neg_binomial_2_log_deviance(counts[3], log_mean_cup, alpha) +
              neg_binomial_2_log_deviance(counts[4], log_mean_cdown, alpha);
  {
    vector[S] offsetS;
    vector[N] offsetN;
    offsetS <- rep_vector(eRJ, S);
    deviance_null <- neg_binomial_2_log_deviance(rejoined, offsetS, alpha);
    offsetS <- rep_vector(eDL, S);
    deviance_null <- deviance_null +
                     neg_binomial_2_log_deviance(danglingL, offsetS, alpha);
    offsetS <- rep_vector(eDR, S);
    deviance_null <- deviance_null +
                     neg_binomial_2_log_deviance(danglingR, offsetS, alpha);
    offsetN <- rep_vector(eC, N);
    deviance_null <- deviance_null +
                     neg_binomial_2_log_deviance(counts[1], offsetN, alpha) +
                     neg_binomial_2_log_deviance(counts[2], offsetN, alpha) +
                     neg_binomial_2_log_deviance(counts[3], offsetN, alpha) +
                     neg_binomial_2_log_deviance(counts[4], offsetN, alpha);
  }
  deviance_proportion_explained <- 100*(deviance_null - deviance)/deviance_null;
}
