// License: BSD 3 clause

#include "tick/hawkes/model/model_hawkes_sumexpkern_leastsq_single.h"

ModelHawkesSumExpKernLeastSqSingle::ModelHawkesSumExpKernLeastSqSingle(
    const ArrayDouble &decays, const ulong n_baselines,
    const double period_length, const unsigned int max_n_threads,
    const unsigned int optimization_level)
    : ModelHawkesSingle(max_n_threads, optimization_level),
      n_baselines(n_baselines),
      period_length(period_length),
      decays(decays),
      n_decays(decays.size()) {}

// Method that computes the value
double ModelHawkesSumExpKernLeastSqSingle::loss(const ArrayDouble &coeffs) {
  // The initialization should be performed if not performed yet
  if (!weights_computed) compute_weights();

  // This allows to run in a multithreaded environment the computation of the
  // contribution of each component
  SArrayDoublePtr values =
      parallel_map(get_n_threads(), n_nodes,
                   &ModelHawkesSumExpKernLeastSqSingle::loss_i, this, coeffs);

  // We just need to sum up the contribution
  return values->sum() / n_total_jumps;
}

// Performs the computation of the contribution of the i component to the value
double ModelHawkesSumExpKernLeastSqSingle::loss_i(const ulong i,
                                                  const ArrayDouble &coeffs) {
  if (!weights_computed)
    TICK_ERROR("Please compute weights before calling loss_i");

  ArrayDouble mu_i = view(coeffs, i * n_baselines, (i + 1) * n_baselines);
  ulong start_alpha_i = n_nodes * n_baselines + i * n_nodes * n_decays;
  ulong end_alpha_i = n_nodes * n_baselines + (i + 1) * n_nodes * n_decays;
  ArrayDouble alpha_i = view(coeffs, start_alpha_i, end_alpha_i);

  double C_sum = 0;
  double Dg_sum = 0;
  double Dgg_sum = 0;
  double E_sum = 0;

  ArrayDouble2d &C_i = C[i];
  for (ulong j = 0; j < n_nodes; ++j) {
    ArrayDouble2d &Dg_j = Dg[j];
    ArrayDouble2d &Dgg_j = Dgg[j];
    ArrayDouble2d &E_j = E[j];

    for (ulong u = 0; u < n_decays; ++u) {
      double alpha_i_j_u = alpha_i[j * n_decays + u];
      C_sum += alpha_i_j_u * C_i(j, u);

      for (ulong p = 0; p < n_baselines; ++p) {
        Dg_sum += alpha_i_j_u * mu_i[p] * Dg_j[u * n_baselines + p];
      }

      for (ulong u1 = 0; u1 < n_decays; ++u1) {
        double alpha_i_j_u1 = alpha_i[j * n_decays + u1];
        Dgg_sum += alpha_i_j_u * alpha_i_j_u1 * Dgg_j(u, u1);

        for (ulong j1 = 0; j1 < n_nodes; ++j1) {
          double alpha_i_j1_u1 = alpha_i[j1 * n_decays + u1];
          E_sum += alpha_i_j_u * alpha_i_j1_u1 * E_j(j1, u * n_decays + u1);
        }
      }
    }
  }

  double A_i = 0;
  double B_i = 0;
  ArrayDouble &K_i = K[i];

  for (ulong p = 0; p < n_baselines; ++p) {
    A_i += mu_i[p] * mu_i[p] * L[p];
    B_i += mu_i[p] * K_i[p];
  }

  A_i += 2 * Dg_sum;
  A_i += Dgg_sum;
  A_i += 2 * E_sum;

  B_i += C_sum;

  return A_i - 2 * B_i;
}

// Method that computes the gradient
void ModelHawkesSumExpKernLeastSqSingle::grad(const ArrayDouble &coeffs,
                                              ArrayDouble &out) {
  // The initialization should be performed if not performed yet
  if (!weights_computed) compute_weights();

  // This allows to run in a multithreaded environment the computation of each
  // component
  parallel_run(get_n_threads(), n_nodes,
               &ModelHawkesSumExpKernLeastSqSingle::grad_i, this, coeffs, out);
  out /= n_total_jumps;
}

// Method that computes the component i of the gradient
void ModelHawkesSumExpKernLeastSqSingle::grad_i(const ulong i,
                                                const ArrayDouble &coeffs,
                                                ArrayDouble &out) {
  if (!weights_computed)
    TICK_ERROR("Please compute weights before calling hessian_i");

  ArrayDouble mu_i = view(coeffs, i * n_baselines, (i + 1) * n_baselines);
  ulong start_alpha_i = n_nodes * n_baselines + i * n_nodes * n_decays;
  ulong end_alpha_i = n_nodes * n_baselines + (i + 1) * n_nodes * n_decays;
  ArrayDouble alpha_i = view(coeffs, start_alpha_i, end_alpha_i);

  ArrayDouble grad_mu_i = view(out, i * n_baselines, (i + 1) * n_baselines);
  ArrayDouble grad_alpha_i = view(out, start_alpha_i, end_alpha_i);
  grad_alpha_i.init_to_zero();

  ArrayDouble &K_i = K[i];
  for (ulong p = 0; p < n_baselines; ++p) {
    grad_mu_i[p] = 2 * mu_i[p] * L[p] - 2 * K_i[p];
  }

  ArrayDouble2d &C_i = C[i];
  for (ulong j = 0; j < n_nodes; ++j) {
    ArrayDouble2d &Dg_j = Dg[j];
    ArrayDouble2d &Dgg_j = Dgg[j];
    ArrayDouble2d &E_j = E[j];

    for (ulong u = 0; u < n_decays; ++u) {
      double alpha_i_j_u = alpha_i[j * n_decays + u];
      double &grad_alpha_i_j_u = grad_alpha_i[j * n_decays + u];

      grad_alpha_i_j_u -= 2 * C_i(j, u);

      for (ulong p = 0; p < n_baselines; ++p) {
        grad_mu_i[p] += 2 * alpha_i_j_u * Dg_j[u * n_baselines + p];
        grad_alpha_i_j_u += 2 * mu_i[p] * Dg_j[u * n_baselines + p];
      }

      for (ulong u1 = 0; u1 < n_decays; ++u1) {
        double alpha_i_j_u1 = alpha_i[j * n_decays + u1];

        grad_alpha_i_j_u += 2 * alpha_i_j_u1 * Dgg_j(u, u1);

        for (ulong j1 = 0; j1 < n_nodes; ++j1) {
          double alpha_i_j1_u1 = alpha_i[j1 * n_decays + u1];
          double &grad_alpha_i_j1_u1 = grad_alpha_i[j1 * n_decays + u1];
          double E_j_j1_u_u1 = E_j(j1, u * n_decays + u1);

          grad_alpha_i_j_u += 2 * alpha_i_j1_u1 * E_j_j1_u_u1;
          grad_alpha_i_j1_u1 += 2 * alpha_i_j_u * E_j_j1_u_u1;
        }
      }
    }
  }
}


void ModelHawkesSumExpKernLeastSqSingle::hessian(ArrayDouble &out) {
  if (!weights_computed) compute_weights();

  // This allows to run in a multithreaded environment the computation of each
  // component
  parallel_run(get_n_threads(), n_nodes,
               &ModelHawkesSumExpKernLeastSqSingle::hessian_i, this, out);
  out /= n_total_jumps;
}

void ModelHawkesSumExpKernLeastSqSingle::hessian_i(const ulong i,
                                                   ArrayDouble &out) {
  if (!weights_computed)
    TICK_ERROR("Please compute weights before calling hessian_i");

  if (n_baselines != 1)
    TICK_ERROR("hessian is only implemented for one baseline");

  // number of alphas per dimension
  const ulong n_alpha_i = n_nodes * n_decays;

  // fill mu line of matrix
  const ulong start_mu_line = i * (n_alpha_i + 1);

  // fill mu mu
  out[start_mu_line] = 2 * end_time;

  // fill mu alpha
  for (ulong j = 0; j < n_nodes; ++j) {
    const ArrayDouble2d &Dg_j = Dg[j];
    for (ulong u = 0; u < n_decays; ++u) {
      out[start_mu_line + j * n_decays + u + 1] += 2 * Dg_j(u, 0);
    }
  }

  // fill alpha lines
  const ulong block_start = n_nodes * (n_alpha_i + 1) + i * (n_alpha_i + 1) * n_alpha_i;
  for (ulong l = 0; l < n_nodes; ++l) {
    const ArrayDouble2d &Dg_l = Dg[l];
    const ArrayDouble2d &Dg2_l = Dgg[l];
    const ArrayDouble2d &E_l = E[l];

    for (ulong u = 0; u < n_decays; ++u) {
      const ulong start_alpha_line = block_start + (l * n_decays + u) * (n_alpha_i + 1);

      // fill alpha mu
      out[start_alpha_line] += 2 * Dg_l(u, 0);

      // fill alpha square
      for (ulong m = 0; m < n_nodes; ++m) {
        const ArrayDouble2d &E_m = E[m];

        for (ulong u1 = 0; u1 < n_decays; ++u1) {
          out[start_alpha_line + m * n_decays + u1 + 1] +=
              2 * (E_l(m, u * n_decays + u1) + E_m(l, u1 * n_decays + u));
          if (l == m) {
            out[start_alpha_line + m * n_decays + u1 + 1] +=
                Dg2_l[u * n_decays + u1] + Dg2_l[u1 * n_decays + u];
          }
        }
      }
    }
  }
}

// Computes both gradient and value
// TODO : optimization !
double ModelHawkesSumExpKernLeastSqSingle::loss_and_grad(
    const ArrayDouble &coeffs, ArrayDouble &out) {
  grad(coeffs, out);
  return loss(coeffs);
}

// Contribution of the ith component to the initialization
// Computation of the arrays H, Dg, Dg2 and C
void ModelHawkesSumExpKernLeastSqSingle::compute_weights_i(const ulong i) {
  for (ulong p = 0; p < n_baselines; ++p) {
    // dispatch interval length computation among threads
    if (p % n_nodes == i) L[p] = get_baseline_interval_length(p);
  }

  ulong n_decays = decays.size();

  ArrayDouble &timestamps_i = *timestamps[i];
  ArrayDouble2d H(n_nodes, n_decays);
  H.init_to_zero();
  ArrayULong l = ArrayULong(n_nodes);
  l.init_to_zero();

  ArrayDouble2d &C_i = C[i];
  ArrayDouble2d &Dg_i = Dg[i];
  Dg_i.init_to_zero();

  ArrayDouble2d &Dgg_i = Dgg[i];
  ArrayDouble2d &E_i = E[i];
  ArrayDouble &K_i = K[i];

  ulong N_i = timestamps_i.size();
  for (ulong k = 0; k < N_i; ++k) {
    double t_k_i = timestamps_i[k];

    const ulong p_interval = get_baseline_interval(t_k_i);
    K_i[p_interval] += 1;

    // compute and store expensive weights that will be reused multiple times
    ArrayDouble2d exponentials(n_decays, n_decays);
    for (ulong u = 0; u < n_decays; ++u) {
      for (ulong u1 = 0; u1 < n_decays; ++u1) {
        exponentials(u, u1) =
            exp(-(decays[u1] + decays[u]) * (end_time - t_k_i));
      }
    }
    ArrayDouble exponential_diff;
    if (k > 0) {
      double t_k_minus_one_i = timestamps_i[k - 1];
      exponential_diff = ArrayDouble(n_decays);
      for (ulong u = 0; u < n_decays; ++u) {
        double decay_u = decays[u];
        exponential_diff[u] = exp(-decay_u * (t_k_i - t_k_minus_one_i));
      }
    }

    for (ulong j = 0; j < n_nodes; ++j) {
      ArrayDouble &timestamps_j = *timestamps[j];
      ulong N_j = timestamps_j.size();

      if (k > 0) {
        for (ulong u = 0; u < n_decays; ++u) {
          H(j, u) *= exponential_diff[u];
        }
      }

      while (l[j] < N_j && timestamps_j[l[j]] < t_k_i) {
        double t_l_j = timestamps_j[l[j]];

        for (ulong u = 0; u < n_decays; ++u) {
          double decay_u = decays[u];
          H(j, u) += decay_u * cexp(-decay_u * (t_k_i - t_l_j));
        }

        l[j] += 1;
      }

      for (ulong u = 0; u < n_decays; ++u) {
        double decay_u = decays[u];
        C_i(j, u) += H(j, u);

        for (ulong u1 = 0; u1 < n_decays; ++u1) {
          double decay_u1 = decays[u1];

          // we fill E_i,j,u',u
          double ratio = decay_u1 / (decay_u1 + decay_u);
          double tmp = 1 - exponentials(u, u1);
          E_i(j, u1 * n_decays + u) += ratio * tmp * H(j, u);
        }
      }
    }

    for (ulong u = 0; u < n_decays; ++u) {
      double decay_u = decays[u];
      ArrayDouble Dg_i_u = view_row(Dg_i, u);
      for (ulong p = 0; p < n_baselines; ++p) {
        ulong n_passed_periods =
            static_cast<ulong>(std::floor(t_k_i / period_length));
        double lower = n_passed_periods * period_length +
                       (p * period_length) / n_baselines;
        while (lower < end_time) {
          const double shift_lower = std::max(t_k_i, lower);
          const double upper =
              std::min(lower + period_length / n_baselines, end_time);
          if (shift_lower < upper)
            Dg_i_u[p] += cexp(-decay_u * (shift_lower - t_k_i)) -
                         cexp(-decay_u * (upper - t_k_i));
          lower += period_length;
        }
      }
      for (ulong u1 = 0; u1 < n_decays; ++u1) {
        double decay_u1 = decays[u1];

        double ratio = decay_u * decay_u1 / (decay_u + decay_u1);
        Dgg_i(u, u1) += ratio * (1 - exponentials(u, u1));
      }
    }
  }
}

void ModelHawkesSumExpKernLeastSqSingle::allocate_weights() {
  if (n_nodes == 0) {
    TICK_ERROR("Please provide valid timestamps before allocating weights")
  }

  L = ArrayDouble(n_baselines);
  L.init_to_zero();

  C = ArrayDouble2dList1D(n_nodes);
  Dgg = ArrayDouble2dList1D(n_nodes);
  E = ArrayDouble2dList1D(n_nodes);
  Dg = ArrayDouble2dList1D(n_nodes);
  K = ArrayDoubleList1D(n_nodes);
  for (ulong i = 0; i < n_nodes; ++i) {
    C[i] = ArrayDouble2d(n_nodes, n_decays);
    C[i].init_to_zero();
    Dg[i] = ArrayDouble2d(n_decays, n_baselines);
    Dg[i].init_to_zero();
    Dgg[i] = ArrayDouble2d(n_decays, n_decays);
    Dgg[i].init_to_zero();
    E[i] = ArrayDouble2d(n_nodes, n_decays * n_decays);
    E[i].init_to_zero();
    K[i] = ArrayDouble(n_baselines);
    K[i].init_to_zero();
  }
}

// Full initialization of the arrays H, Dg, Dg2 and C
// Must be performed just once
void ModelHawkesSumExpKernLeastSqSingle::compute_weights() {
  allocate_weights();

  // Multithreaded computation of the arrays
  parallel_run(get_n_threads(), n_nodes,
               &ModelHawkesSumExpKernLeastSqSingle::compute_weights_i, this);
  weights_computed = true;
}

ulong ModelHawkesSumExpKernLeastSqSingle::get_n_coeffs() const {
  return n_nodes * n_baselines + n_nodes * n_nodes * n_decays;
}

ulong ModelHawkesSumExpKernLeastSqSingle::get_baseline_interval(
    const double t) {
  const double first_period_t =
      t - std::floor(t / period_length) * period_length;
  if (first_period_t == period_length) return n_baselines - 1;
  return static_cast<ulong>(
      std::floor(first_period_t / period_length * n_baselines));
}

double ModelHawkesSumExpKernLeastSqSingle::get_baseline_interval_length(
    const ulong interval_p) {
  const ulong n_full_periods =
      static_cast<ulong>(std::floor(end_time / period_length));
  const double full_interval_length = period_length / n_baselines;
  const double remaining_time = end_time - n_full_periods * period_length;
  const double period_start = interval_p * full_interval_length;
  const double extra_period = std::min(
      std::max(remaining_time - period_start, 0.), full_interval_length);
  return n_full_periods * full_interval_length + extra_period;
}

ulong ModelHawkesSumExpKernLeastSqSingle::get_n_baselines() const {
  return n_baselines;
}

void ModelHawkesSumExpKernLeastSqSingle::set_n_baselines(ulong n_baselines) {
  this->n_baselines = n_baselines;
  weights_computed = false;
}

double ModelHawkesSumExpKernLeastSqSingle::get_period_length() const {
  return period_length;
}

void ModelHawkesSumExpKernLeastSqSingle::set_period_length(
    double period_length) {
  this->period_length = period_length;
  weights_computed = false;
}


void ModelHawkesSumExpKernLeastSqSingle::compute_penalization_constant(double x,
                                                                    ArrayDouble &pen_mu,
                                                                    ArrayDouble &pen_L1_alpha,
                                                                    double pen_mu_const1,
                                                                    double pen_mu_const2,
                                                                    double pen_L1_const1,
                                                                    double pen_L1_const2,
                                                                    double normalization) {
  if (pen_mu.size() != n_nodes) TICK_ERROR("Bad Size for array argument 'pen_mu'");
  if (pen_L1_alpha.size() != n_nodes * n_nodes * n_decays) TICK_ERROR(
      "Bad Size for array argument 'pen_L1_alpha'");

  // Penalization for mu
  for (ulong i = 0; i < n_nodes; i++) {

    double m = (6 * timestamps[i]->size() + 56 * x) / (112 * x);
    m = std::max(m, exp(1.));
    double l = 2 * log(log(m));

    pen_mu[i] =
        pen_mu_const1 * sqrt((x + log(n_nodes) + l) * timestamps[i]->size() / end_time / end_time)
            + pen_mu_const2 * (x + log(n_nodes) + l) / end_time;
  }

  // Penalization for Lasso term

  for (ulong u = 0; u < n_decays; ++u) {
    const double beta_u = decays[u];
    for (unsigned long j = 0; j < n_nodes; j++) {
      for (unsigned long k = 0; k < n_nodes; k++) {

        double bjk = compute_bjk(k, beta_u);
        double vjk = compute_vjk(j, k, beta_u);

        // Computation of Ljk
        double temp = (6 * end_time * vjk + 56 * x * bjk * bjk) / (112 * x * bjk * bjk);
        temp = std::max(temp, exp(1.));
        double ljk = 2 * log(log(temp));

        double term1 = pen_L1_const1 * sqrt(((x + 2 * log(n_nodes) + ljk) * vjk) / end_time);
        double term2 = pen_L1_const2 * (x + 2 * log(n_nodes) + ljk) * bjk / end_time;

        term1 /= sqrt(normalization);
        term2 /= normalization;

        // Finally computing the Lasso penalization
        ulong index = j * n_nodes * n_decays + k * n_decays + u;
        pen_L1_alpha[index] = term1 + term2;
//        pen_L1_alpha[index] = j * 100 + k * 10 + u;
      }
    }
  }
}
