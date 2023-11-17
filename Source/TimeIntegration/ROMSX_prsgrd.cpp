#include <ROMSX.H>

using namespace amrex;

void
ROMSX::prsgrd (const Box& phi_bx, const Box& phi_gbx,
               const Box& utbx, const Box& vtbx,
               Array4<Real> ru , Array4<Real> rv,
               Array4<Real> on_u , Array4<Real> om_v,
               Array4<Real> rho,
               Array4<Real> FC,
               Array4<Real> Hz,
               Array4<Real> z_r,
               Array4<Real> z_w,
               const int nrhs,
               const int N)
{
    const int Mn = Geom(0).Domain().size()[0];
    const int Mm = Geom(0).Domain().size()[1];
    auto phi_bxD=phi_bx;
    phi_bxD.makeSlab(2,0);
    auto phi_gbxD=phi_gbx & phi_bx;
    phi_gbxD.makeSlab(2,0);
    Box phi_ubx = surroundingNodes(phi_bx,0);
    Box phi_vbx = surroundingNodes(phi_bx,1);
    Box phi_ubxD = surroundingNodes(phi_bxD,0);
    Box phi_vbxD = surroundingNodes(phi_bxD,1);
    auto utbxD = utbx;
    auto vtbxD = vtbx;
    utbxD.makeSlab(2,0);
    vtbxD.makeSlab(2,0);

    if (verbose > 1) {
        Print() << "phi_gbxD " << phi_gbxD << std::endl;
        Print() << "phi_bxD " << phi_bxD << std::endl;
        Print() << "phi_ubxD " << phi_ubxD << std::endl;
        Print() << "rho box " << Box(rho) << std::endl;
        Print() << "FC box " << Box(FC) << std::endl;
    }
    //hardcode these for now instead of reading them from inputs
    Real T0=14.0;
    Real S0=35.0;
    Real R0=1027;
    Real Tcoef=1.7e-4;
    Real Scoef=0.0;
    Real rho0=1025.0;

    const Real OneFifth = 0.2_rt;
    const Real OneTwelfth = 1.0_rt/12.0_rt;
    const Real eps = 1.0E-10_rt;
    Real g=9.81;
    Real GRho=g/rho0;
    Real GRho0=1000.0_rt*GRho;
    Real HalfGRho=0.5_rt*GRho;

    FArrayBox fab_zwrk(phi_bx,1,The_Async_Arena());
    FArrayBox fab_P(phi_bx,1,The_Async_Arena());
    FArrayBox fab_aux(Box(z_r),1,The_Async_Arena());
    FArrayBox fab_dR(phi_bx,1,The_Async_Arena());
    FArrayBox fab_dZ(phi_bx,1,The_Async_Arena());
    FArrayBox fab_dRx(phi_bx,1,The_Async_Arena());
    FArrayBox fab_dZx(phi_bx,1,The_Async_Arena());

    auto zwrk=fab_zwrk.array();
    auto P=fab_P.array();
    auto aux=fab_aux.array();
    auto dR=fab_dR.array();
    auto dZ=fab_dZ.array();
    auto dRx=fab_dRx.array();
    auto dZx=fab_dZx.array();

    // Derivatives in the z direction
    amrex::ParallelFor(phi_bx,
    [=] AMREX_GPU_DEVICE (int i, int j, int k)
    {
        if(k>=0&&k<N) {
            dR(i,j,k)=rho(i,j,k+1)-rho(i,j,k);
            dZ(i,j,k)=z_r(i,j,k+1)-z_r(i,j,k);
        } else {
            dR(i,j,N)=rho(i,j,N)-rho(i,j,N-1);
            dZ(i,j,N)=z_r(i,j,N)-z_r(i,j,N-1);
            //This is really k=-1
            //dR(i,j,0)=dR(i,j,1);
            //dZ(i,j,0)=dZ(i,j,1);
        }
    });

    amrex::ParallelFor(phi_bxD,
    [=] AMREX_GPU_DEVICE (int i, int j, int )
    {
        for(int k=N;k>=0;k--) {
            Real cff= k>0 ? 2.0*dR(i,j,k)*dR(i,j,k-1) : 2.0*dR(i,j,k)*dR(i,j,k);
            if (cff>eps) {
                dR(i,j,k)= k>0 ? cff/(dR(i,j,k)+dR(i,j,k-1)) : cff/(dR(i,j,k)+dR(i,j,k));
            } else {
                dR(i,j,k)=0.0;
            }
            dZ(i,j,k)= k>0 ? 2.0_rt*dZ(i,j,k)*dZ(i,j,k-1)/(dZ(i,j,k)+dZ(i,j,k-1)) :
                             2.0_rt*dZ(i,j,k)*dZ(i,j,k)/(dZ(i,j,k)+dZ(i,j,k));
        }
    });

    amrex::ParallelFor(phi_bxD,
    [=] AMREX_GPU_DEVICE (int i, int j, int )
    {
        Real cff1=1.0_rt/(z_r(i,j,N)-z_r(i,j,N-1));
        Real cff2=0.5_rt*(rho(i,j,N)-rho(i,j,N-1))*
            (z_w(i,j,N)-z_r(i,j,N))*cff1;
        P(i,j,N)=GRho0*z_w(i,j,N)+GRho*(rho(i,j,N)+cff2)*
                                       (z_w(i,j,N)-z_r(i,j,N));
        for(int k=N-1;k>=0;k--) {
            P(i,j,k)=P(i,j,k+1)+
                     HalfGRho*((rho(i,j,k+1)+rho(i,j,k))*
                               (z_r(i,j,k+1)-z_r(i,j,k))-
                               OneFifth*
                               ((dR(i,j,k+1)-dR(i,j,k))*
                                (z_r(i,j,k+1)-z_r(i,j,k)-
                                 OneTwelfth*
                                 (dZ(i,j,k+1)+dZ(i,j,k)))-
                                (dZ(i,j,k+1)-dZ(i,j,k))*
                                (rho(i,j,k+1)-rho(i,j,k)-
                                 OneTwelfth*
                                 (dR(i,j,k+1)+dR(i,j,k)))));
         }
    });
    if (verbose > 2) {
        amrex::PrintToFile("P_inprsgrd").SetPrecision(18)<<FArrayBox(P)<<std::endl;
        amrex::PrintToFile("z_w_inprsgrd").SetPrecision(18)<<FArrayBox(z_w)<<std::endl;
        amrex::PrintToFile("z_r_inprsgrd").SetPrecision(18)<<FArrayBox(z_r)<<std::endl;
    }
    //This should be nodal
    // Derivatives in the x direction
    amrex::ParallelFor(phi_ubx,
    [=] AMREX_GPU_DEVICE (int i, int j, int k)
    {
        FC(i,j,k)=rho(i,j,k)-rho(i-1,j,k);
        aux(i,j,k)=z_r(i,j,k)-z_r(i-1,j,k);
    });
    if (verbose > 2) {
        amrex::PrintToFile("FC_inprsgrd").SetPrecision(18)<<FArrayBox(FC)<<std::endl;
        amrex::PrintToFile("rho_inprsgrd").SetPrecision(18)<<FArrayBox(rho)<<std::endl;
        amrex::PrintToFile("aux_inprsgrd").SetPrecision(18)<<FArrayBox(aux)<<std::endl;
    }
    //This should be nodal aux and FC need wider boxes above
    //dZx and dRx may have index mismatch issues at k=2 and k=N
    amrex::ParallelFor(phi_bxD,
    [=] AMREX_GPU_DEVICE (int i, int j, int )
    {
        for(int k=N;k>=0;k--) {
            Real cff= 2.0*aux(i,j,k)*aux(i+1,j,k);
            if (cff>eps) {
                Real cff1= 1.0_rt/(aux(i+1,j,k)+aux(i,j,k));
                dZx(i,j,k)=cff*cff1;
            } else {
                dZx(i,j,k)=0.0;
            }
            Real cff1= 2.0*FC(i,j,k)*FC(i+1,j,k);
            if (cff1>eps) {
                Real cff2= 1.0_rt/(FC(i,j,k)+FC(i+1,j,k));
                dRx(i,j,k)=cff1*cff2;
            } else {
                dRx(i,j,k)=0.0;
            }
        }
    });
    if (verbose > 2) {
        amrex::PrintToFile("dRx_inprsgrd").SetPrecision(18)<<FArrayBox(dRx)<<std::endl;
        amrex::PrintToFile("dZx_inprsgrd").SetPrecision(18)<<FArrayBox(dZx)<<std::endl;
    }
    //This should be nodal aux and FC need wider boxes above
    amrex::ParallelFor(utbxD,
    [=] AMREX_GPU_DEVICE (int i, int j, int )
    {
        for(int k=N;k>=0;k--) {
            ru(i,j,k,nrhs)=on_u(i,j,0)*0.5_rt*
                           (Hz(i,j,k)+Hz(i-1,j,k))*
                           (P(i-1,j,k)-P(i,j,k)-
                            HalfGRho*
                            ((rho(i,j,k)+rho(i-1,j,k))*
                             (z_r(i,j,k)-z_r(i-1,j,k))-
                              OneFifth*
                              ((dRx(i,j,k)-dRx(i-1,j,k))*
                               (z_r(i,j,k)-z_r(i-1,j,k)-
                                OneTwelfth*
                                (dZx(i,j,k)+dZx(i-1,j,k)))-
                               (dZx(i,j,k)-dZx(i-1,j,k))*
                               (rho(i,j,k)-rho(i-1,j,k)-
                                OneTwelfth*
                                (dRx(i,j,k)+dRx(i-1,j,k))))));
            //if (verbose > 2) {
            //    printf("%d %d %d  %15.15g %15.15g  %15.15g %15.15g  %15.15g %15.15g  %15.15g %15.15g   %15.15g %15.15g   %15.15g %15.15g ru prsgrd  Hz P rho z_r dZx dRx\n",
            //        i,j,k, Hz(i,j,k), Hz(i-1,j,k),
            //        P(i,j,k), P(i-1,j,k), rho(i,j,k), rho(i-1,j,k), z_r(i,j,k), z_r(i-1,j,k), dZx(i,j,k), dZx(i-1,j,k), dRx(i,j,k), dRx(i-1,j,k));
            //}
        }
    });

    //This should be nodal
    amrex::ParallelFor(phi_vbx,
    [=] AMREX_GPU_DEVICE (int i, int j, int k)
    {
        FC(i,j,k)= rho(i,j,k)-rho(i,j-1,k);
        aux(i,j,k)= z_r(i,j,k)-z_r(i,j-1,k);
    });
    if (verbose > 2) {
        amrex::PrintToFile("FC_inprsgrd").SetPrecision(18)<<FArrayBox(FC)<<std::endl;
        amrex::PrintToFile("rho_inprsgrd").SetPrecision(18)<<FArrayBox(rho)<<std::endl;
        amrex::PrintToFile("aux_inprsgrd").SetPrecision(18)<<FArrayBox(aux)<<std::endl;
    }
    //This should be nodal aux and FC need wider boxes above
    //dZx and dRx may have index mismatch issues at k=2 and k=N
    amrex::ParallelFor(phi_bxD,
    [=] AMREX_GPU_DEVICE (int i, int j, int )
    {
        for(int k=N;k>=0;k--) {
            Real cff= 2.0*aux(i,j,k)*aux(i,j+1,k);
            if (cff>eps) {
                Real cff1= 1.0_rt/(aux(i,j+1,k)+aux(i,j,k));
                dZx(i,j,k)=cff*cff1;
            } else {
                dZx(i,j,k)=0.0;
            }
            Real cff1= 2.0*FC(i,j,k)*FC(i,j+1,k);
            if (cff1>eps) {
                Real cff2= 1.0_rt/(FC(i,j,k)+FC(i,j+1,k));
                dRx(i,j,k)=cff1*cff2;
            } else {
                dRx(i,j,k)=0.0;
            }
        }
    });
    if (verbose > 2) {
        amrex::PrintToFile("dRx_inprsgrd").SetPrecision(18)<<FArrayBox(dRx)<<std::endl;
        amrex::PrintToFile("dZx_inprsgrd").SetPrecision(18)<<FArrayBox(dZx)<<std::endl;
    }
    //This should be nodal aux and FC need wider boxes above
    amrex::ParallelFor(vtbxD,
    [=] AMREX_GPU_DEVICE (int i, int j, int )
    {
        for(int k=N;k>=0;k--) {
            rv(i,j,k,nrhs)=om_v(i,j,0)*0.5_rt*
                           (Hz(i,j,k)+Hz(i,j-1,k))*
                           (P(i,j-1,k)-P(i,j,k)-
                            HalfGRho*
                            ((rho(i,j,k)+rho(i,j-1,k))*
                             (z_r(i,j,k)-z_r(i,j-1,k))-
                              OneFifth*
                              ((dRx(i,j,k)-dRx(i,j-1,k))*
                               (z_r(i,j,k)-z_r(i,j-1,k)-
                                OneTwelfth*
                                (dZx(i,j,k)+dZx(i,j-1,k)))-
                               (dZx(i,j,k)-dZx(i,j-1,k))*
                               (rho(i,j,k)-rho(i,j-1,k)-
                                OneTwelfth*
                                (dRx(i,j,k)+dRx(i,j-1,k))))));
        }
    });
}
