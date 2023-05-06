#include <ROMSX.H>
#include <Utils.H>

using namespace amrex;
//
// Start 2d step
//

void
ROMSX::advance_2d (int lev,
                   MultiFab& mf_u, MultiFab& mf_v,
                   std::unique_ptr<MultiFab>& /*mf_ru*/,
                   std::unique_ptr<MultiFab>& /*mf_rv*/,
                   std::unique_ptr<MultiFab>& mf_Zt_avg1,
                   std::unique_ptr<MultiFab>& mf_DU_avg1,
                   std::unique_ptr<MultiFab>& mf_DU_avg2,
                   std::unique_ptr<MultiFab>& mf_DV_avg1,
                   std::unique_ptr<MultiFab>& mf_DV_avg2,
                   std::unique_ptr<MultiFab>& mf_rubar,
                   std::unique_ptr<MultiFab>& mf_rvbar,
                   std::unique_ptr<MultiFab>& mf_rzeta,
                   std::unique_ptr<MultiFab>& mf_ubar,
                   std::unique_ptr<MultiFab>& mf_vbar,
                   std::unique_ptr<MultiFab>& mf_zeta,
                   std::unique_ptr<MultiFab>& mf_h,
                   const int ncomp, Real dt_lev, Real dtfast_lev,
                   bool predictor_2d_step,
                   bool first_2d_step, int my_iif, int nfast, int & next_indx1)
{
    auto geomdata  = Geom(lev).data();
    const auto dxi = Geom(lev).InvCellSizeArray();

    const int Lm = Geom(lev).Domain().size()[0];
    const int Mm = Geom(lev).Domain().size()[1];

    auto N = Geom(lev).Domain().size()[2]-1; // Number of vertical "levs" aka, NZ

    int iic = istep[lev];
    //bool predictor_2d_step = true;
    //    int my_iif = 1; //substep index
    int knew = 3;
    int krhs = (my_iif + iic) % 2 + 1;
    int kstp = my_iif <=1 ? iic % 2 + 1 : (iic % 2 + my_iif % 2 + 1) % 2 + 1;
    int indx1 = krhs;
    if(predictor_2d_step)
        next_indx1 = 3 - indx1;
    else {
        knew = next_indx1;
        kstp = 3 - knew;
        krhs = 3;
        //If it's not the auxiliary time step, set indx1 to next_indx1
        if (my_iif<nfast+1)
            indx1=next_indx1;
    }
    int ptsk = 3-kstp;
    Print()<<knew<<"\t"<<krhs<<"\t"<<kstp<<"\t"<<predictor_2d_step<<"\t"<<(my_iif<=1)<<std::endl;
    knew-=1;
    krhs-=1;
    kstp-=1;
    indx1-=1;
    ptsk-=1;
    //Hardcode for 1 fast timestep (predictor+corrector only)
    Real weighta = 0.0;
    Real weightb = 1.0;
    Real weightc = 0.0;
    Real weightd = 0.0;
    Real Fgamma = 0.28400;
    Real  gamma = 0.00000;

    for ( MFIter mfi(mf_u, TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {
        Array4<Real> const& u = (mf_u).array(mfi);
        Array4<Real> const& v = (mf_v).array(mfi);
        Array4<Real> const& ubar = (mf_ubar)->array(mfi);
        Array4<Real> const& vbar = (mf_vbar)->array(mfi);
        Array4<Real> const& zeta = (mf_zeta)->array(mfi);
        Array4<Real> const& h = (mf_h)->array(mfi);
        Array4<Real> const& Zt_avg1 = (mf_Zt_avg1)->array(mfi);
        Array4<Real> const& DU_avg1 = (mf_DU_avg1)->array(mfi);
        Array4<Real> const& DU_avg2 = (mf_DU_avg2)->array(mfi);
        Array4<Real> const& DV_avg1 = (mf_DV_avg1)->array(mfi);
        Array4<Real> const& DV_avg2 = (mf_DV_avg2)->array(mfi);
        Array4<Real> const& rubar = (mf_rubar)->array(mfi);
        Array4<Real> const& rvbar = (mf_rvbar)->array(mfi);
        Array4<Real> const& rzeta = (mf_rzeta)->array(mfi);

        Box bx = mfi.tilebox();
        //copy the tilebox
        Box gbx1 = bx;
        Box gbx11 = bx;
        Box gbx2 = bx;
        //make only gbx be grown to match multifabs
        gbx2.grow(IntVect(2,2,0));
        gbx1.grow(IntVect(1,1,0));
        gbx11.grow(IntVect(1,1,1));
        Box bxD = bx;
        Box ubxD = surroundingNodes(bx,0);
        Box vbxD = surroundingNodes(bx,1);
        bxD.makeSlab(2,0);
        ubxD.makeSlab(2,0);
        vbxD.makeSlab(2,0);
        Box gbx1D = bxD;
        gbx1D.grow(IntVect(1,1,0));
        //AKA
        //ubxD.setRange(2,0);
        //vbxD.setRange(2,0);

        FArrayBox fab_pn(gbx2,1,The_Async_Arena());
        FArrayBox fab_pm(gbx2,1,The_Async_Arena());
        FArrayBox fab_on_u(gbx2,1,The_Async_Arena());
        FArrayBox fab_om_v(gbx2,1,The_Async_Arena());
        FArrayBox fab_fomn(gbx2,1,The_Async_Arena());
        FArrayBox fab_Huon(gbx2,1,The_Async_Arena()); //fab_Huon.setVal(0.0);
        FArrayBox fab_Hvom(gbx2,1,The_Async_Arena()); //fab_Hvom.setVal(0.0);
        FArrayBox fab_oHz(gbx11,1,The_Async_Arena()); //fab_oHz.setVal(0.0);

        //step2d work arrays
        FArrayBox fab_Drhs(gbx2,1,The_Async_Arena());
        FArrayBox fab_Dnew(gbx2,1,The_Async_Arena());
        FArrayBox fab_Dstp(gbx2,1,The_Async_Arena());
        FArrayBox fab_DUon(gbx2,1,The_Async_Arena());
        FArrayBox fab_DVom(gbx2,1,The_Async_Arena());
        FArrayBox fab_rhs_ubar(gbx2,1,The_Async_Arena());
        FArrayBox fab_rhs_vbar(gbx2,1,The_Async_Arena());
        FArrayBox fab_rhs_zeta(gbx2,1,The_Async_Arena());
        FArrayBox fab_zeta_new(gbx2,1,The_Async_Arena());

        auto on_u=fab_on_u.array();
        auto om_v=fab_om_v.array();
        auto pn=fab_pn.array();
        auto pm=fab_pm.array();
        auto fomn=fab_fomn.array();

        auto Drhs=fab_Drhs.array();
        auto Dnew=fab_Dnew.array();
        auto Dstp=fab_Dstp.array();
        auto DUon=fab_DUon.array();
        auto DVom=fab_DVom.array();
        auto rhs_ubar=fab_rhs_ubar.array();
        auto rhs_vbar=fab_rhs_vbar.array();
        auto rhs_zeta=fab_rhs_zeta.array();
        auto zeta_new=fab_zeta_new.array();

        //From ana_grid.h and metrics.F
        amrex::ParallelFor(gbx2,
        [=] AMREX_GPU_DEVICE (int i, int j, int)
        {
              pm(i,j,0)=dxi[0];
              pn(i,j,0)=dxi[1];
              rhs_ubar(i,j,0)=0.0;
              rhs_vbar(i,j,0)=0.0;
        });

        amrex::ParallelFor(gbx2,
        [=] AMREX_GPU_DEVICE (int i, int j, int)
        {
              om_v(i,j,0)=1.0/dxi[0];
              on_u(i,j,0)=1.0/dxi[1];
        });
        amrex::ParallelFor(gbx2,
        [=] AMREX_GPU_DEVICE (int i, int j, int  )
            {

              const auto prob_lo         = geomdata.ProbLo();
              const auto dx              = geomdata.CellSize();

              pm(i,j,0)=dxi[0];
              pn(i,j,0)=dxi[1];
              //defined UPWELLING
              Real f0=-8.26e-5;
              Real beta=0.0;
              Real Esize=1000*(Mm);
              Real y = prob_lo[1] + (j + 0.5) * dx[1];
              Real f=fomn(i,j,0)=f0+beta*(y-.5*Esize);
              fomn(i,j,0)=f*(1.0/(pm(i,j,0)*pn(i,j,0)));
            });

        amrex::ParallelFor(gbx2,
        [=] AMREX_GPU_DEVICE (int i, int j, int)
        {
            Drhs(i,j,0)=zeta(i,j,0,krhs)+h(i,j,0);
        });
        amrex::ParallelFor(ubxD,
        [=] AMREX_GPU_DEVICE (int i, int j, int)
        {
            Real cff=.5*on_u(i,j,0);
            Real cff1=cff*(Drhs(i,j,0)+Drhs(i-1,j,0));
            DUon(i,j,0)=ubar(i,j,0,krhs)*cff1;
        });
        amrex::ParallelFor(vbxD,
        [=] AMREX_GPU_DEVICE (int i, int j, int)
        {
            Real cff=.5*om_v(i,j,0);
            Real cff1=cff*(Drhs(i,j,0)+Drhs(i,j-1,0));
            DVom(i,j,0)=vbar(i,j,0,krhs)*cff1;
        });
        if(predictor_2d_step)
        {
        if(first_2d_step) {
        Real cff2=(Real(-1.0)/Real(12.0))*weighta;
        amrex::ParallelFor(gbx2,
        [=] AMREX_GPU_DEVICE (int i, int j, int)
        {
            Zt_avg1(i,j,0)=0.0;
        });
        amrex::ParallelFor(ubxD,
        [=] AMREX_GPU_DEVICE (int i, int j, int)
        {
            DU_avg1(i,j,0)=0.0;
            DU_avg2(i,j,0)=cff2*DUon(i,j,0);
        });
        amrex::ParallelFor(vbxD,
        [=] AMREX_GPU_DEVICE (int i, int j, int)
        {
            DV_avg1(i,j,0)=0.0;
            DV_avg2(i,j,0)=cff2*DVom(i,j,0);
        });
        }
        else {
        Real cff1=weightb;
        Real cff2=(Real(8.0)/Real(12.0))*weightc-
                  (Real(1.0)/Real(12.0))*weightd;
        amrex::ParallelFor(gbx2,
        [=] AMREX_GPU_DEVICE (int i, int j, int k)
        {
            if(k==0)
            Zt_avg1(i,j,0)=Zt_avg1(i,j,0)+cff1*zeta(i,j,0,krhs);
        });
        amrex::ParallelFor(ubxD,
        [=] AMREX_GPU_DEVICE (int i, int j, int k)
        {
            DU_avg1(i,j,0)=DU_avg1(i,j,0)+cff1*DUon(i,j,0);
            DU_avg2(i,j,0)=DU_avg2(i,j,0)+cff2*DUon(i,j,0);
        });
        amrex::ParallelFor(vbxD,
        [=] AMREX_GPU_DEVICE (int i, int j, int)
        {
            DV_avg1(i,j,0)=DV_avg1(i,j,0)+cff1*DVom(i,j,0);
            DV_avg2(i,j,0)=DV_avg2(i,j,0)+cff2*DVom(i,j,0);
        });
        }
        } else {
        Real cff2;
        if(first_2d_step)
            cff2=weightc;
        else
            cff2=Real(5.0)/Real(12.0)*weightc;
        amrex::ParallelFor(ubxD,
        [=] AMREX_GPU_DEVICE (int i, int j, int)
        {
            DU_avg2(i,j,0)=DU_avg2(i,j,0)+cff2*DUon(i,j,0);
        });
        amrex::ParallelFor(vbxD,
        [=] AMREX_GPU_DEVICE (int i, int j, int)
        {
            DV_avg2(i,j,0)=DV_avg2(i,j,0)+cff2*DVom(i,j,0);
        });
        }

    //
    //  Do not perform the actual time stepping during the auxiliary
    //  (nfast(ng)+1) time step.
    //

        if (my_iif>=nfast) return;
        //Load new free-surface values into shared array at both predictor
        //and corrector steps

        //
        //=======================================================================
        //  Time step free-surface equation.
        //=======================================================================
        //
        //  During the first time-step, the predictor step is Forward-Euler
        //  and the corrector step is Backward-Euler. Otherwise, the predictor
        //  step is Leap-frog and the corrector step is Adams-Moulton.
        //

        // todo: gzeta

        // todo: HACKHACKHACK Should use rho0 from prob.H
        Real fac=1000.0/1025.0;

        if(my_iif==0) {
            Real cff1=dtfast_lev;
            amrex::ParallelFor(gbx1D,
            [=] AMREX_GPU_DEVICE (int i, int j, int )
            {
                rhs_zeta(i,j,0)=(DUon(i,j,0)-DUon(i+1,j,0))+
                                (DVom(i,j,0)-DVom(i,j+1,0));
                zeta_new(i,j,0)=zeta(i,j,0,kstp)+
                                pm(i,j,0)*pn(i,j,0)*cff1*rhs_zeta(i,j,0);
                Dnew(i,j,0)=zeta_new(i,j,0)+h(i,j,0);

                //Pressure gradient terms:
                /*
                  zwrk(i,j)=0.5_rt*(zeta(i,j,kstp)+zeta_new(i,j))
                  gzeta(i,j)=(fac+rhoS(i,j))*zwrk(i,j)
                  gzeta2(i,j)=gzeta(i,j)*zwrk(i,j)
                  gzetaSA(i,j)=zwrk(i,j)*(rhoS(i,j)-rhoA(i,j))*/
            });
        } else if (predictor_2d_step) {
            Real cff1=2.0_rt*dtfast_lev;
            Real cff4=4.0/25.0;
            Real cff5=1.0-2.0*cff4;
            amrex::ParallelFor(gbx1D,
            [=] AMREX_GPU_DEVICE (int i, int j, int )
            {
                rhs_zeta(i,j,0)=(DUon(i,j,0)-DUon(i+1,j,0))+
                                (DVom(i,j,0)-DVom(i,j+1,0));
                zeta_new(i,j,0)=zeta(i,j,0,kstp)+
                                pm(i,j,0)*pn(i,j,0)*cff1*rhs_zeta(i,j,0);
                Dnew(i,j,0)=zeta_new(i,j,0)+h(i,j,0);
                //Pressure gradient terms
                /*
                  zwrk(i,j)=cff5*zeta(i,j,krhs)+                              &
                  &                cff4*(zeta(i,j,kstp)+zeta_new(i,j))
                  gzeta(i,j)=(fac+rhoS(i,j))*zwrk(i,j)
                  gzeta2(i,j)=gzeta(i,j)*zwrk(i,j)
                  gzetaSA(i,j)=zwrk(i,j)*(rhoS(i,j)-rhoA(i,j))*/
            });
        } else if (!predictor_2d_step) { //AKA if(corrector_2d_step)
            Real cff1=dtfast_lev*5.0_rt/12.0_rt;
            Real cff2=dtfast_lev*8.0_rt/12.0_rt;
            Real cff3=dtfast_lev*1.0_rt/12.0_rt;
            Real cff4=2.0_rt/5.0_rt;
            Real cff5=1.0_rt-cff4;
            amrex::ParallelFor(gbx1D,
            [=] AMREX_GPU_DEVICE (int i, int j, int )
            {
                Real cff=cff1*((DUon(i,j,0)-DUon(i+1,j,0))+
                               (DVom(i,j,0)-DVom(i,j+1,0)));
                zeta_new(i,j,0)=zeta(i,j,0,kstp)+
                    pm(i,j,0)*pn(i,j,0)*(cff+
                                         cff2*rzeta(i,j,0,kstp)-
                                         cff3*rzeta(i,j,0,ptsk));
                Dnew(i,j,0)=zeta_new(i,j,0)+h(i,j,0);
                //Pressure gradient terms
                /*
                  zwrk(i,j)=cff5*zeta_new(i,j)+cff4*zeta(i,j,krhs)
                  gzeta(i,j)=(fac+rhoS(i,j))*zwrk(i,j)
                  gzeta2(i,j)=gzeta(i,j)*zwrk(i,j)
                  gzetaSA(i,j)=zwrk(i,j)*(rhoS(i,j)-rhoA(i,j))*/
                });
        }

        //
        //  Load new free-surface values into shared array at both predictor
        //  and corrector steps.
        //
        amrex::ParallelFor(gbx1D,
        [=] AMREX_GPU_DEVICE (int i, int j, int )
        {
            zeta(i,j,0,knew)=zeta_new(i,j,0);
        });

        //
        //  If predictor step, load right-side-term into shared array.
        //
        if (predictor_2d_step) {
            amrex::ParallelFor(gbx1D,
            [=] AMREX_GPU_DEVICE (int i, int j, int )
            {
                rzeta(i,j,0,krhs)=rhs_zeta(i,j,0);
            });
        }

        // Advection terms for 2d ubar, vbar added to rhs_ubar and rhs_vbar
        //
        //-----------------------------------------------------------------------
        // rhs_2d
        //-----------------------------------------------------------------------
        //
        rhs_2d(bxD, ubar, vbar, rhs_ubar, rhs_vbar, DUon, DVom, krhs, N);

#ifdef UV_COR
        // Coriolis terms for 2d ubar, vbar added to rhs_ubar and rhs_vbar
        //
        //-----------------------------------------------------------------------
        // coriolis
        //-----------------------------------------------------------------------
        //
        // Need to clean up rhs_ubar vs rubar (index only the same for one out of predictor/corrector)
        coriolis(bxD, ubar, vbar, rhs_ubar, rhs_vbar, Drhs, fomn, krhs, 0);
#endif
    }

    //Add in horizontal harmonic viscosity.
    //todo: visc2_p visc2_r
    // Consider generalizing or copying uv3dmix, where Drhs is used instead of Hz and u=>ubar v=>vbar, drop dt terms

    //Coupling from 3d to 2d
    /////////Coupling of 3d updates to 2d predictor-corrector
    //todo: iif=>my_iif iic => icc
    /*
    IF (iif.eq.1.and.PREDICTOR_2D_STEP) THEN
        IF (iic.eq.ntfirst) THEN
          DO j=Jstr,Jend
            DO i=IstrU,Iend
!              rufrc(i,j)=rufrc(i,j)-rhs_ubar(i,j)
!              rhs_ubar(i,j)=rhs_ubar(i,j)+rufrc(i,j)
!              ru(i,j,0,nstp)=rufrc(i,j)
            END DO
          END DO
          DO j=JstrV,Jend
            DO i=Istr,Iend
!              rvfrc(i,j)=rvfrc(i,j)-rhs_vbar(i,j)
!              rhs_vbar(i,j)=rhs_vbar(i,j)+rvfrc(i,j)
!              rv(i,j,0,nstp)=rvfrc(i,j)
            END DO
          END DO
        ELSE IF (iic.eq.(ntfirst+1)) THEN
          DO j=Jstr,Jend
            DO i=IstrU,Iend
!              rufrc(i,j)=rufrc(i,j)-rhs_ubar(i,j)
!              rhs_ubar(i,j)=rhs_ubar(i,j)+                              &
!     &                      1.5_rt*rufrc(i,j)-0.5_rt*ru(i,j,0,nnew)
!              ru(i,j,0,nstp)=rufrc(i,j)
            END DO
          END DO
          DO j=JstrV,Jend
            DO i=Istr,Iend
!              rvfrc(i,j)=rvfrc(i,j)-rhs_vbar(i,j)
!              rhs_vbar(i,j)=rhs_vbar(i,j)+                              &
!     &                      1.5_rt*rvfrc(i,j)-0.5_rt*rv(i,j,0,nnew)
!              rv(i,j,0,nstp)=rvfrc(i,j)
            END DO
          END DO
        ELSE
          cff1=23.0_rt/12.0_rt
          cff2=16.0_rt/12.0_rt
          cff3= 5.0_rt/12.0_rt
          DO j=Jstr,Jend
            DO i=IstrU,Iend
!              rufrc(i,j)=rufrc(i,j)-rhs_ubar(i,j)
!              rhs_ubar(i,j)=rhs_ubar(i,j)+                              &
!     &                      cff1*rufrc(i,j)-                            &
!     &                      cff2*ru(i,j,0,nnew)+                        &
!     &                      cff3*ru(i,j,0,nstp)
!              ru(i,j,0,nstp)=rufrc(i,j)
            END DO
          END DO
          DO j=JstrV,Jend
            DO i=Istr,Iend
!              rvfrc(i,j)=rvfrc(i,j)-rhs_vbar(i,j)
!              rhs_vbar(i,j)=rhs_vbar(i,j)+                              &
!     &                      cff1*rvfrc(i,j)-                            &
!     &                      cff2*rv(i,j,0,nnew)+                        &
!     &                      cff3*rv(i,j,0,nstp)
!              rv(i,j,0,nstp)=rvfrc(i,j)
            END DO
          END DO
        END IF
      ELSE
        DO j=Jstr,Jend
          DO i=IstrU,Iend
            rhs_ubar(i,j)=rhs_ubar(i,j)+rufrc(i,j)
          END DO
        END DO
        DO j=JstrV,Jend
          DO i=Istr,Iend
            rhs_vbar(i,j)=rhs_vbar(i,j)+rvfrc(i,j)
          END DO
        END DO
      END IF
    */

    //
    //=======================================================================
    //  Time step 2D momentum equations.
    //=======================================================================
    //
    //  Compute total water column depth.
    //
    /*
      DO j=JstrV-1,Jend
        DO i=IstrU-1,Iend
          Dstp(i,j)=zeta(i,j,kstp)+h(i,j)
        END DO
      END DO
!
!  During the first time-step, the predictor step is Forward-Euler
!  and the corrector step is Backward-Euler. Otherwise, the predictor
!  step is Leap-frog and the corrector step is Adams-Moulton.
!
      IF (iif.eq.1) THEN
        cff1=0.5_rt*dtfast
        DO j=Jstr,Jend
          DO i=IstrU,Iend
            cff=(pm(i,j)+pm(i-1,j))*(pn(i,j)+pn(i-1,j))
            fac=1.0_rt/(Dnew(i,j)+Dnew(i-1,j))
            ubar(i,j,knew)=ubar(i,j,kstp)
!            ubar(i,j,knew)=(ubar(i,j,kstp)*                             &
!     &                      (Dstp(i,j)+Dstp(i-1,j))+                    &
!     &                      cff*cff1*rhs_ubar(i,j))*fac
          END DO
        END DO
        DO j=JstrV,Jend
          DO i=Istr,Iend
            cff=(pm(i,j)+pm(i,j-1))*(pn(i,j)+pn(i,j-1))
            fac=1.0_rt/(Dnew(i,j)+Dnew(i,j-1))
            vbar(i,j,knew)=vbar(i,j,kstp)
!            vbar(i,j,knew)=(vbar(i,j,kstp)*                             &
!     &                      (Dstp(i,j)+Dstp(i,j-1))+                    &
!     &                      cff*cff1*rhs_vbar(i,j))*fac
          END DO
        END DO
      ELSE IF (PREDICTOR_2D_STEP) THEN
        cff1=dtfast
        DO j=Jstr,Jend
          DO i=IstrU,Iend
            cff=(pm(i,j)+pm(i-1,j))*(pn(i,j)+pn(i-1,j))
            fac=1.0_rt/(Dnew(i,j)+Dnew(i-1,j))
            ubar(i,j,knew)=ubar(i,j,kstp)
!            ubar(i,j,knew)=(ubar(i,j,kstp)*                             &
!     &                      (Dstp(i,j)+Dstp(i-1,j))+                    &
!     &                      cff*cff1*rhs_ubar(i,j))*fac
          END DO
        END DO
        DO j=JstrV,Jend
          DO i=Istr,Iend
            cff=(pm(i,j)+pm(i,j-1))*(pn(i,j)+pn(i,j-1))
            fac=1.0_rt/(Dnew(i,j)+Dnew(i,j-1))
            vbar(i,j,knew)=vbar(i,j,kstp)
!            vbar(i,j,knew)=(vbar(i,j,kstp)*                             &
!     &                      (Dstp(i,j)+Dstp(i,j-1))+                    &
!     &                      cff*cff1*rhs_vbar(i,j))*fac
          END DO
        END DO
      ELSE IF (CORRECTOR_2D_STEP) THEN
        cff1=0.5_rt*dtfast_lev*5.0_rt/12.0_rt
        cff2=0.5_rt*dtfast_lev*8.0_rt/12.0_rt
        cff3=0.5_rt*dtfast_lev*1.0_rt/12.0_rt
        DO j=Jstr,Jend
          DO i=IstrU,Iend
            cff=(pm(i,j)+pm(i-1,j))*(pn(i,j)+pn(i-1,j))
            fac=1.0_rt/(Dnew(i,j)+Dnew(i-1,j))
            ubar(i,j,knew)=ubar(i,j,kstp)
!            ubar(i,j,knew)=(ubar(i,j,kstp)*                             &
!     &                      (Dstp(i,j)+Dstp(i-1,j))+                    &
!     &                      cff*(cff1*rhs_ubar(i,j)+                    &
!     &                           cff2*rubar(i,j,kstp)-                  &
!     &                           cff3*rubar(i,j,ptsk)))*fac
          END DO
        END DO
        DO j=JstrV,Jend
          DO i=Istr,Iend
            cff=(pm(i,j)+pm(i,j-1))*(pn(i,j)+pn(i,j-1))
            fac=1.0_rt/(Dnew(i,j)+Dnew(i,j-1))
            vbar(i,j,knew)=vbar(i,j,kstp)
!            vbar(i,j,knew)=(vbar(i,j,kstp)*                             &
!     &                      (Dstp(i,j)+Dstp(i,j-1))+                    &
!     &                      cff*(cff1*rhs_vbar(i,j)+                    &
!     &                           cff2*rvbar(i,j,kstp)-                  &
!     &                           cff3*rvbar(i,j,ptsk)))*fac
          END DO
        END DO
      END IF
*/
    //store rhs_ubar and rhs_vbar to save later
    //
    //  If predictor step, load right-side-term into shared arrays for
    //  future use during the subsequent corrector step.
    //
    /*
      IF (PREDICTOR_2D_STEP) THEN
        DO j=Jstr,Jend
          DO i=IstrU,Iend
            rubar(i,j,krhs)=rhs_ubar(i,j)
          END DO
        END DO
        DO j=JstrV,Jend
          DO i=Istr,Iend
            rvbar(i,j,krhs)=rhs_vbar(i,j)
          END DO
        END DO
END IF
    */
    }
