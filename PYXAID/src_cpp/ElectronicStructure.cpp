/***********************************************************
 * Copyright (C) 2013 Alexey V. Akimov
 * This file is distributed under the terms of the
 * GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 * http://www.gnu.org/copyleft/gpl.txt
***********************************************************/

#include "ElectronicStructure.h"
#include "aux.h"
#include "io.h"
#include "random.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

using namespace std;


//===================== Class ElectronicStructure ============================

double ElectronicStructure::energy(){
  double res = ( (*Ccurr).H() * (*Hcurr) * (*Ccurr)).M[0].real(); //0.0;
  return res;
}

void ElectronicStructure::update_populations(){
  *A = ((*Ccurr).conj()) * ((*Ccurr).T());
}

double ElectronicStructure::norm(){
  return ((*Ccurr).H() * (*Ccurr)).M[0].real();
}


void ElectronicStructure::update_decoherence_times(matrix& rates){

  update_populations();

  for(int i=0;i<num_states;i++){
    tau_m[i] = 0.0;
    for(int j=0;j<num_states;j++){
      tau_m[i] += A->M[j*num_states+j].real()*rates.M[i*num_states+j].real(); 
    }// for j
  }// for i
}

void ElectronicStructure::project_out(int i){
  // Project out state i
  Ccurr->M[i] = 0.0;

  // Normalize the rest of the wavefunction
  double nrm = 0.0;
  for(int j=0;j<num_states;j++){ if(j!=i){ nrm += A->M[j*num_states+j].real(); }   }  nrm = sqrt(nrm);
  for(j=0;j<num_states;j++){  Ccurr->M[j] /= nrm; }
   
  update_populations();

}

void ElectronicStructure::decohere(int i){
  // State i decoheres
  Ccurr->M[i] = 1.0;
  for(int j=0;j<num_states;j++){  if(j!=i){ Ccurr->M[j] = 0.0;} }
  curr_state = i;
}


void ElectronicStructure::check_decoherence(double dt,int boltz_flag,double Temp,matrix& rates){

  update_decoherence_times(rates);

  for(int i=0;i<num_states;i++){
    double rnd_i = 1.0/tau_m[i];  // Simplest implementation

    if(t_m[i]>=rnd_i) { // Decoherence event occurs for state i

        double zeta = uniform(0.0,1.0);
        double P = A->M[i*num_states+i].real(); // probability to decohere

        // In leu of hop rejection use Boltzmann factors
//        if(boltz_flag==1){
          double dE = (Hcurr->M[i*num_states+i] - Hcurr->M[curr_state*num_states+curr_state]).real();
          if(dE>0){  P *= exp(-(dE/(kb*Temp))); }  // hop to higher energy state is difficult
//        }

        if(zeta < P){       // Hop to the state i from current state with probability P
          decohere(i);
          break;            // only one even per time step
        } 
        else{  project_out(i);   }

        // Reset the time axis for state i
        t_m[i] = 0;
        tau_m[i] = 0.0;

      }// t_m[i]>=1.0/tau_m
      else{ ;; } // Coherence of all states maintained
    }// for i

  // Advancing time
  for(i=0;i<num_states;i++){  t_m[i] += dt; }

}



void ElectronicStructure::update_hop_prob(double dt,int boltz_flag, double Temp,matrix& Ef){
/*******************************************************
 (Re-)Calculate hopping probabilities from given state 
*******************************************************/
  update_populations();

  for(int i=0;i<num_states;i++){
    double a_ii = A->M[i*num_states+i].real(); 
    if (a_ii==0.0){ a_ii = 1e-12; }

    double sum = 0.0;
    for(int j=0;j<num_states;j++){
      if(j!=i){
        // In general the expression is:
        // Pij = (2*dt/(hbar*|c_i|^2) ) * summ_j ( Im(Hij * c_j^* * c_j)  )
        // where Hij is for TD-SE: i*hbar*dc/dt = H * c
        // Hcurr at this moments is -i*hbar*<i|d/dt|j>
        // Hprime* at this moment is -i*hbar*<i|p|j>, Ef will include: 2*e/m_e * A(t) * cos(omega*t)

        // warning!!!: Before 4/15/2013 there was "-" sign in the line below
        complex<double> Hij = Hcurr->M[i*num_states+j] + 
                     Ef.M[0]*Hprimex->M[i*num_states+j] + 
                     Ef.M[1]*Hprimey->M[i*num_states+j] +
                     Ef.M[2]*Hprimez->M[i*num_states+j];
        double E_i = (Hcurr->M[i*num_states+i] + 
                      Ef.M[0]*Hprimex->M[i*num_states+i] + 
                      Ef.M[1]*Hprimex->M[i*num_states+i] +
                      Ef.M[2]*Hprimex->M[i*num_states+i]
                     ).real();
        double E_j = (Hcurr->M[j*num_states+j] +
                      Ef.M[0]*Hprimex->M[j*num_states+j] +
                      Ef.M[1]*Hprimex->M[j*num_states+j] +
                      Ef.M[2]*Hprimex->M[j*num_states+j]
                     ).real();


        double g_ij= (2.0*dt/(a_ii*hbar))*(A->M[i*num_states+j] * Hij ).imag(); // g_ij = P(i->j)

        if(g_ij<0.0){ g_ij = 0.0; }
//        if(boltz_flag==1){ 
          double dE = (E_j - E_i);
          if(dE>0){  g_ij *= exp(-(dE/(kb*Temp))); }  // hop to higher energy state is difficult
//        }
        g[i*num_states+j] = g_ij;
        
        sum += g_ij;
      }// j!=i
    }// for j
    g[i*num_states+i] = 1.0 - sum;
  }// for i

}

void ElectronicStructure::init_hop_prob1(){
  for(int i=0;i<num_states;i++){
    for(int j=0;j<num_states;j++){
      if(j!=i){ g[i*num_states+j] = 0.0; }
      else{ g[i*num_states+j] = 1.0; }
    }// for j
  }// for i
}

void ElectronicStructure::update_hop_prob1(double dt,int boltz_flag, double Temp,matrix& Ef,double Eex){
/*******************************************************
 Here we actually sum up all the transition probabilities
*******************************************************/
  update_populations();

  //Ef = 0.0; // test!!!

  for(int i=0;i<num_states;i++){
    double a_ii = A->M[i*num_states+i].real();
    if (a_ii==0.0){ a_ii = 1e-12; }

    double sum = 0.0;
    for(int j=0;j<num_states;j++){
      if(j!=i){
        // In general the expression is:
        // Pij = (2*dt/(hbar*|c_i|^2) ) * summ_j ( Im(Hij * c_j^* * c_j)  )
        // where Hij is for TD-SE: i*hbar*dc/dt = H * c
        // Hcurr at this moments is -i*hbar*<i|d/dt|j>
        // Hprime* at this moment is -i*hbar*<i|p|j>, Ef will include: 2*e/m_e * A(t) * cos(omega*t)

        // warning!!!: Before 4/15/2013 there was "-" sign in the line below
        complex<double> Hij_nac = Hcurr->M[i*num_states+j];
        complex<double> Hij_field = Ef.M[0]*Hprimex->M[i*num_states+j] +
                                    Ef.M[1]*Hprimey->M[i*num_states+j] +
                                    Ef.M[2]*Hprimez->M[i*num_states+j];
        double E_i = (Hcurr->M[i*num_states+i] +
                      Ef.M[0]*Hprimex->M[i*num_states+i] +
                      Ef.M[1]*Hprimex->M[i*num_states+i] +
                      Ef.M[2]*Hprimex->M[i*num_states+i]
                     ).real();
        double E_j = (Hcurr->M[j*num_states+j] +
                      Ef.M[0]*Hprimex->M[j*num_states+j] +
                      Ef.M[1]*Hprimex->M[j*num_states+j] +
                      Ef.M[2]*Hprimex->M[j*num_states+j]
                     ).real();


//        double g_ij= (2.0*dt/(a_ii*hbar))*(A->M[i*num_states+j] * Hij ).imag(); // g_ij = P(i->j)

        double dE = (E_j - E_i);
        double bf = 1.0;
        if(dE>Eex){  bf= exp(-((dE-Eex)/(kb*Temp))); }  // hop to higher energy state is difficult - thermal equilibrium
                                                // no such scaling for Hij_field - it is non-equilibrium process

        double g_ij= (2.0*dt/(a_ii*hbar))*(A->M[i*num_states+j] * bf * (  Hij_nac + Hij_field) ).imag(); // g_ij = P(i->j)

//        if(i==0 && j==1) { cout<<"nac = "<<Hij_nac<<" field = "<<Hij_field<<endl; }

        if(g_ij<0.0){ g_ij = 0.0; }

/*
  boltz_flag is now to be deprecated - because it is necessary - to satisfy the detailed balance
        if(g_ij<0.0){ g_ij = 0.0; }
        if(boltz_flag==1){
          double dE = (E_j - E_i);
          if(dE>0){  g_ij *= exp(-(dE/(kb*Temp))); }  // hop to higher energy state is difficult - thermal equilibrium
        }
*/
        g[i*num_states+j] += g_ij;

        sum += g_ij;
      }// j!=i
    }// for j
    g[i*num_states+i] -= sum;
  }// for i

}



void ElectronicStructure::rot1(double phi,int i,int j){
/***********************************************************************
  Action of operator: exp(iL_ij^-) = exp(phi*(c_j*d/dc_i - c_i*d/dc_j)):

 |c_i|    |  c   s |     | c_i |
 |   |  = |        |  x  |     |
 |c_j|    | -s   c |     | c_j |

 rotation in plane (i,j) by angle phi, c = cos(phi), s = sin(phi)
***********************************************************************/

  double c = cos(phi);
  double s = sin(phi);
  complex<double> c_i =  c * Ccurr->M[i] + s * Ccurr->M[j];
  complex<double> c_j = -s * Ccurr->M[i] + c * Ccurr->M[j];
  Ccurr->M[i] = c_i;
  Ccurr->M[j] = c_j;
}

void ElectronicStructure::rot2(double phi,int i,int j){
/***********************************************************************
  Action of operator: exp(iL_ij^+) = exp(i*phi*(c_j*d/dc_i + c_i*d/dc_j)):

 |c_i|    |  c   is |     | c_i |
 |   |  = |         |  x  |     |
 |c_j|    | is    c |     | c_j |

 "rotation" in plane (i,j) by angle phi, c = cos(phi), s = sin(phi)
***********************************************************************/

  complex<double> cs(cos(phi),0.0);
  complex<double> isi(0.0,sin(phi));
  complex<double> c_i,c_j;

  c_i = cs * Ccurr->M[i] + isi * Ccurr->M[j];
  c_j = isi * Ccurr->M[i] + cs * Ccurr->M[j];

  Ccurr->M[i] = c_i;
  Ccurr->M[j] = c_j;


}

void ElectronicStructure::rot(complex<double> Hij,double dt,int i,int j){
/***********************************************************************
  Action of operator: exp(iL_ij*dt) = exp(-(i/hbar)*dt*(H_ij*c_j*d/dc_i + H_ji*c_i*d/dc_j)):

  exp(iL_ij*dt) = A * B * A, where
 
  A = exp(iL_ij^-), phi = (dt/2) * Im(H_ij)/hbar
  B = exp(iL_ij^+), phi =  dt * -Re(H_ij)/hbar

 composite rotation in plane i,j due to Hamiltonian matrix elements H_ij
 note Hamiltonian is Hermitian: H_ij^* = H_ji
***********************************************************************/

  double phi1 = 0.5*dt*Hij.imag()/hbar;
  double phi2 = -dt*Hij.real()/hbar;

  rot1(phi1,i,j);
  rot2(phi2,i,j);
  rot1(phi1,i,j);
}

void ElectronicStructure::phase(complex<double> Hii,double dt,int i){
/***********************************************************************
  Action of operator: exp(iL_ii*dt) = exp(-(i/hbar)*dt*(H_ii*c_i*d/dc_i)):

  c_i  = exp(-i*H_ii*dt/hbar) * c_i

  Because Hamiltonian is Hermitian its diagonal elements are fully real
***********************************************************************/

  double phi = -dt*Hii.real()/hbar;
  Ccurr->M[i] = std::complex<double>(cos(phi),sin(phi)) * Ccurr->M[i];

}

void ElectronicStructure::propagate_coefficients(double dt,matrix& Ef){

  int i,j;
  complex<double> Hprime;

  // exp(iLij * dt/2)  ---->
  for(i=0;i<num_states;i++){
    for(j=i+1;j<num_states;j++){
      Hprime = Ef.M[0]*Hprimex->M[i*num_states+j] +
               Ef.M[1]*Hprimey->M[i*num_states+j] +
               Ef.M[2]*Hprimez->M[i*num_states+j];

      rot(Hcurr->M[i*num_states+j]+Hprime,0.5*dt,i,j);
    }
  }

  // exp(iL1 * dt)
  for(i=0;i<num_states;i++){ 
    Hprime = Ef.M[0]*Hprimex->M[i*num_states+i] +
             Ef.M[1]*Hprimey->M[i*num_states+i] +
             Ef.M[2]*Hprimez->M[i*num_states+i];

    phase(Hcurr->M[i*num_states+i]+Hprime,dt,i); 
  }

  // exp(iLij * dt/2)  <----
  for(i=num_states-1;i>=0;i--){
    for(j=num_states-1;j>i;j--){
      Hprime = Ef.M[0]*Hprimex->M[i*num_states+j] +
               Ef.M[1]*Hprimey->M[i*num_states+j] +
               Ef.M[2]*Hprimez->M[i*num_states+j];

      rot(Hcurr->M[i*num_states+j]+Hprime,0.5*dt,i,j);
    }
  }

  
}

void ElectronicStructure::propagate_coefficients1(double dt,int opt,matrix& Ef){
/***********************************************************************
 This is interpolation scheme
 Hcurr_interp(dt) = Hcurr + dHdt*dt
 i*hbar*dC/dt = Hcurr_interp * C =>
 C(dt) = C(0) - (i/hbar)*dt*Hcurr_interp
***********************************************************************/

  complex<double> i(0.0,1.0);
  *Hcurr = *Hcurr + dt*(*dHdt);
  if(opt==1){  *Cnext = *Ccurr - (i*dt/hbar)*(*Hcurr) * (*Ccurr);      }
  else if(opt==2){ *Cnext = *Cprev - 2.0*(i*dt/hbar)*(*Hcurr) * (*Ccurr); }

  *Cprev = *Ccurr;
  *Ccurr = *Cnext;
 

}


void ElectronicStructure::propagate_coefficients2(double dt,matrix& Ef){
/*************************************************************
  This is basically exact solution of linear ODE system
  i*hbar*dC/dt = H * C 
  C(dt) = exp(-i*dt*H/hbar) * C(0)
*************************************************************/
  double tol = 1e-12;
  complex<double> arg(0.0,(-dt/hbar));

  // hermitian symmetrize Hcurr - just to be sure the algorithm will work better
  matrix tmp(Hcurr->n_rows,Hcurr->n_cols);
  tmp = ((*Hcurr) + (*Hcurr).H());
  tmp *= 0.5;

  *Ccurr = exp(tmp,arg,tol) * (*Ccurr);
}

