////
// Cut-site normalization model
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
}
////////////////////////////////////////////////////////////////
data {
  //spline parameters
  int Krow; //number of functions in spline base for row biases
  //int Kdiag; //number of functions in spline base for diagonal decay
  //biases
  int<lower=1> S; //number of cut sites
  vector[S] cutsites; //cut site locations
  int rejoined[S];
  int danglingL[S];
  int danglingR[S];
  //counts
  /*int<lower=1> N; //number of data points
  int count[N]; //raw count
  int<lower=1,upper=S> cidx[N,2]; //index of its associated cut site
  int<lower=1,upper=4> type[N]; //count type (Up Down Close Far)*/
}
transformed data {
  real intercept;
  //bias spline (nu and delta have the same design)
  vector[nnz(S)] Xrow_w;
  int Xrow_v[nnz(S)];
  int Xrow_u[S+1];
  vector[Krow] prow;
  //diagonal SCAM spline
  /*vector[nnz(N)] Xdiag_w;
  int Xdiag_v[nnz(N)];
  int Xdiag_u[N+1];
  vector[N] pdiag;*/
  
  intercept <- 1;//TODO
  
  ////bias spline (nu and delta have the same design)
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

  //diagonal SCAM spline
  //projector for biases (GAM)
  prow <- column_sums(Krow, Xrow_w, Xrow_v)';
  prow <- prow / sqrt(dot_self(prow));
  //projector for diagonal (SCAM)
  //TODO
}
parameters {
  real eRJ; //exposure for rejoined ends
  real eDE; //exposure for dangling ends
  vector[Krow-1] beta_nu;
  vector[Krow-1] beta_delta;
  //positive_ordered[Kdiag-1] beta_diag;
  real<lower=0> alpha;
  real<lower=0> lambda_nu;
  real<lower=0> lambda_delta;
}
transformed parameters {
  //nu
  vector[S] log_nu; // log(nu)
  vector[Krow-2] beta_nu_diff; //2nd order difference on beta_nu_aug
  //delta
  vector[S] log_delta; // log(delta)
  vector[Krow-2] beta_delta_diff; //2nd order difference on beta_delta_aug
  
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
  //delta
  {
    vector[Krow] beta_delta_centered;
    vector[Krow] beta_delta_aug;
    beta_delta_aug[1] <- sum(beta_delta);
    beta_delta_aug[2:] <- beta_delta;
    beta_delta_centered <- beta_delta_aug - (beta_delta_aug' * prow) * prow;
    log_delta <- csr_matrix_times_vector(S, Krow, Xrow_w, Xrow_v, Xrow_u, beta_delta_centered);
    beta_delta_diff <- beta_delta_aug[:(Krow-2)]-2*beta_delta_aug[2:(Krow-1)]+beta_delta_aug[3:];
  }
}
model {
  //// build regression components
  //vector[N] log_decay; //diagonal decay
  //log_decay <- Xdiag*beta_diag;
  
  //// likelihoods
  //biases
  rejoined ~ neg_binomial_2(exp(intercept + eRJ + log_nu), alpha);
  danglingL ~ neg_binomial_2(exp(intercept + eDE + log_nu - log_delta), alpha);
  danglingR ~ neg_binomial_2(exp(intercept + eDE + log_nu + log_delta), alpha);
  //counts
  
  //// Priors
  //P-spline prior on the differences (K-2 params)
  //warning on jacobian can be ignored
  //see GAM, Wood (2006), section 4.8.2 (p.187)
  beta_nu_diff ~ normal(0, 1./(alpha*lambda_nu));
  beta_delta_diff ~ normal(0, 1./(alpha*lambda_delta));
}