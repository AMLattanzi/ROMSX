#include <REMORA.H>

using namespace amrex;

// Advance a single 3D level for a single time step
void REMORA::advance_3d_ml (int lev, Real dt_lev)
{
    // Fill in three ways: 1) interpolate from coarse grid if lev > 0; 2) fill from physical boundaries;
    //                     3) fine-fine fill of ghost cells with FillBoundary call
    //FillPatch(lev, t_old[lev], *cons_new[lev], cons_new, BCVars::cons_bc, BdyVars::t);
    xvel_new[lev]->FillBoundary(geom[lev].periodicity());
    yvel_new[lev]->FillBoundary(geom[lev].periodicity());
    //FillPatch(lev, t_old[lev], *zvel_new[lev], zvel_new, BCVars::zvel_bc, BdyVars::null);

    //FillPatch(lev, t_old[lev], *vec_sstore[lev], GetVecOfPtrs(vec_sstore), BCVars::cons_bc, BdyVars::t);
    //FillPatch(lev, time, *vec_sstore[lev], GetVecOfPtrs(vec_sstore), BCVars::cons_bc, BdyVars::t,0,true,true,0,0,0.0,*cons_old[lev]);

    auto N = Geom(lev).Domain().size()[2]-1; // Number of vertical "levs" aka, NZ

    advance_3d(lev, *cons_new[lev], *xvel_new[lev], *yvel_new[lev],
               vec_sstore[lev].get(),
               vec_ru[lev].get(), vec_rv[lev].get(),
               vec_DU_avg1[lev], vec_DU_avg2[lev],
               vec_DV_avg1[lev], vec_DV_avg2[lev],
               vec_ubar[lev],  vec_vbar[lev],
               vec_Akv[lev], vec_Akt[lev], vec_Hz[lev], vec_Huon[lev], vec_Hvom[lev],
               vec_z_w[lev], vec_hOfTheConfusingName[lev].get(),
               vec_pm[lev].get(), vec_pn[lev].get(),
               vec_msku[lev].get(), vec_mskv[lev].get(),
               N, dt_lev);

    // Ideally can roll all of these into a single call
    FillPatchNoBC(lev, t_old[lev], *vec_ubar[lev], GetVecOfPtrs(vec_ubar), BdyVars::ubar,0,false,false);
    FillPatchNoBC(lev, t_old[lev], *vec_vbar[lev], GetVecOfPtrs(vec_vbar), BdyVars::vbar,0,false,false);
    FillPatchNoBC(lev, t_old[lev], *vec_ubar[lev], GetVecOfPtrs(vec_ubar), BdyVars::ubar,1,false,false);
    FillPatchNoBC(lev, t_old[lev], *vec_vbar[lev], GetVecOfPtrs(vec_vbar), BdyVars::vbar,1,false,false);
    FillPatchNoBC(lev, t_old[lev], *vec_ubar[lev], GetVecOfPtrs(vec_ubar), BdyVars::ubar,2,false,false);
    FillPatchNoBC(lev, t_old[lev], *vec_vbar[lev], GetVecOfPtrs(vec_vbar), BdyVars::vbar,2,false,false);
    //FillPatch(lev, t_old[lev], *vec_sstore[lev], GetVecOfPtrs(vec_sstore), BCVars::cons_bc, BdyVars::t);


    // Fill in three ways: 1) interpolate from coarse grid if lev > 0; 2) fill from physical boundaries;
    //                     3) fine-fine fill of ghost cells with FillBoundary call
    // Note that we need the fine-fine and physical bc's in order to correctly move the particles
    FillPatch(lev, t_old[lev], *cons_new[lev], cons_new, BCVars::cons_bc, BdyVars::t,0,true,false,0,0,dt_lev,*cons_old[lev]);
    xvel_new[lev]->FillBoundary(geom[lev].periodicity());
    yvel_new[lev]->FillBoundary(geom[lev].periodicity());
    FillPatch(lev, t_old[lev], *zvel_new[lev], zvel_new, BCVars::zvel_bc, BdyVars::null);

    // Apply land/sea mask to tracers
#ifdef _OPENMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
    for ( MFIter mfi(*cons_new[lev], TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {
        Array4<Real> const& cons = cons_new[lev]->array(mfi);
        Array4<Real> const& mskr = vec_mskr[lev]->array(mfi);

        Box bx = mfi.tilebox();

        ParallelFor(bx, NCONS, [=] AMREX_GPU_DEVICE (int i, int j, int k, int n)
        {
            cons(i,j,k,n) *= mskr(i,j,0);
        });
    }

#ifdef REMORA_USE_PARTICLES
    // **************************************************************************************
    // Update the particle positions
    // **************************************************************************************
   Vector<MultiFab const*> flow_vels =  {xvel_new[lev], yvel_new[lev], zvel_new[lev]};
   evolveTracers( lev, dt_lev, flow_vels, vec_z_phys_nd );
#endif
}
