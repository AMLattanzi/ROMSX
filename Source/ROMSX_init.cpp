/**
 * \file ROMSX_init.cpp
 */

#include <ROMSX.H>
#include <EOS.H>
#include <ROMSX_Constants.H>
#include <Utils.H>
#include <prob_common.H>

using namespace amrex;

#ifdef ROMSX_USE_NETCDF

Real
read_from_wrfbdy(std::string nc_bdy_file, const Box& domain,
                 Vector<Vector<FArrayBox>>& bdy_data_xlo,
                 Vector<Vector<FArrayBox>>& bdy_data_xhi,
                 Vector<Vector<FArrayBox>>& bdy_data_ylo,
                 Vector<Vector<FArrayBox>>& bdy_data_yhi);

void
convert_wrfbdy_data(int which, const Box& domain,
                    Vector<Vector<FArrayBox>>& bdy_data,
                    const FArrayBox& NC_MUB_fab,
                    const FArrayBox& NC_MSFU_fab,
                    const FArrayBox& NC_MSFV_fab,
                    const FArrayBox& NC_MSFM_fab,
                    const FArrayBox& NC_PH_fab,
                    const FArrayBox& NC_PHB_fab,
                    const FArrayBox& NC_C1H_fab,
                    const FArrayBox& NC_C2H_fab,
                    const FArrayBox& NC_RDNW_fab,
                    const FArrayBox& NC_xvel_fab,
                    const FArrayBox& NC_yvel_fab,
                    const FArrayBox& NC_rho_fab,
                    const FArrayBox& NC_rhoth_fab);

void
ROMSX::init_from_wrfinput(int lev)
{
    // *** FArrayBox's at this level for holding the INITIAL data
    Vector<FArrayBox> NC_xvel_fab ; NC_xvel_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_yvel_fab ; NC_yvel_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_zvel_fab ; NC_zvel_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_rho_fab  ; NC_rho_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_rhop_fab ; NC_rhop_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_rhoth_fab; NC_rhoth_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_MUB_fab  ; NC_MUB_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_MSFU_fab ; NC_MSFU_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_MSFV_fab ; NC_MSFV_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_MSFM_fab ; NC_MSFM_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_SST_fab  ; NC_SST_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_C1H_fab  ; NC_C1H_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_C2H_fab  ; NC_C2H_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_RDNW_fab ; NC_RDNW_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_PH_fab   ; NC_PH_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_PHB_fab  ; NC_PHB_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_ALB_fab  ; NC_ALB_fab.resize(num_boxes_at_level[lev]);
    Vector<FArrayBox> NC_PB_fab   ; NC_PB_fab.resize(num_boxes_at_level[lev]);

    if (nc_init_file.size() == 0)
        amrex::Error("NetCDF initialization file name must be provided via input");

    for (int idx = 0; idx < num_boxes_at_level[lev]; idx++)
    {
        read_from_wrfinput(lev,idx,NC_xvel_fab,NC_yvel_fab,NC_zvel_fab,NC_rho_fab,
                           NC_rhop_fab,NC_rhoth_fab,NC_MUB_fab,
                           NC_MSFU_fab,NC_MSFV_fab,NC_MSFM_fab,
                           NC_SST_fab,
                           NC_C1H_fab,NC_C2H_fab,NC_RDNW_fab,
                           NC_PH_fab,NC_PHB_fab,NC_ALB_fab,NC_PB_fab);
    }

    auto& lev_new = vars_new[lev];

#ifdef _OPENMP
#pragma omp parallel if (amrex::Gpu::notInLaunchRegion())
#endif
    // INITIAL DATA common for "ideal" as well as "real" simulation
    for ( MFIter mfi(lev_new[Vars::cons], TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {
        // Define fabs for holding the initial data
        FArrayBox &cons_fab = lev_new[Vars::cons][mfi];
        FArrayBox &xvel_fab = lev_new[Vars::xvel][mfi];
        FArrayBox &yvel_fab = lev_new[Vars::yvel][mfi];
        FArrayBox &zvel_fab = lev_new[Vars::zvel][mfi];

        init_state_from_wrfinput(lev, cons_fab, xvel_fab, yvel_fab, zvel_fab,
                                 NC_xvel_fab, NC_yvel_fab, NC_zvel_fab,
                                 NC_rho_fab, NC_rhoth_fab);
    } // mf

#ifdef _OPENMP
#pragma omp parallel if (amrex::Gpu::notInLaunchRegion())
#endif
    // Map scale factors common for "ideal" as well as "real" simulation
    for ( MFIter mfi(*mapfac_u[lev], TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {
        // Define fabs for holding the initial data
        FArrayBox &msfu_fab = (*mapfac_u[lev])[mfi];
        FArrayBox &msfv_fab = (*mapfac_v[lev])[mfi];
        FArrayBox &msfm_fab = (*mapfac_m[lev])[mfi];

        init_msfs_from_wrfinput(lev, msfu_fab, msfv_fab, msfm_fab,
                                NC_MSFU_fab, NC_MSFV_fab, NC_MSFM_fab);
    } // mf

    if (solverChoice.use_terrain)
    {
        std::unique_ptr<MultiFab>& z_phys = z_phys_nd[lev];
        for ( MFIter mfi(lev_new[Vars::cons], TilingIfNotGPU()); mfi.isValid(); ++mfi )
        {
            FArrayBox& z_phys_nd_fab = (*z_phys)[mfi];
            init_terrain_from_wrfinput(lev, z_phys_nd_fab, NC_PH_fab, NC_PHB_fab);
        } // mf

        make_metrics(geom[lev],*z_phys_nd[lev],*z_phys_cc[lev],*detJ_cc[lev]);

    } // use_terrain

    MultiFab r_hse (base_state[lev], make_alias, 0, 1); // r_0  is first  component
    MultiFab p_hse (base_state[lev], make_alias, 1, 1); // p_0  is second component
    MultiFab pi_hse(base_state[lev], make_alias, 2, 1); // pi_0 is third  component

    if (init_type == "real") {
        for ( MFIter mfi(lev_new[Vars::cons], TilingIfNotGPU()); mfi.isValid(); ++mfi )
        {
            FArrayBox&  p_hse_fab = p_hse[mfi];
            FArrayBox& pi_hse_fab = pi_hse[mfi];
            FArrayBox&  r_hse_fab = r_hse[mfi];
            FArrayBox& z_phys_nd_fab = (*z_phys_nd[lev])[mfi];
            FArrayBox& z_phys_cc_fab = (*z_phys_cc[lev])[mfi];

            const Box& bx = mfi.validbox();
            init_base_state_from_wrfinput(lev, bx, p_hse_fab, pi_hse_fab, r_hse_fab, z_phys_nd_fab, z_phys_cc_fab,
                                         NC_ALB_fab, NC_PB_fab);
        }
    }

    if (init_type == "real" && (lev == 0)) {
        if (nc_bdy_file.empty())
            amrex::Error("NetCDF boundary file name must be provided via input");
        bdy_time_interval = read_from_wrfbdy(nc_bdy_file,geom[0].Domain(),bdy_data_xlo,bdy_data_xhi,bdy_data_ylo,bdy_data_yhi);

        const Box& domain = geom[lev].Domain();

        convert_wrfbdy_data(0,domain,bdy_data_xlo,
                            NC_MUB_fab[0], NC_MSFU_fab[0], NC_MSFV_fab[0], NC_MSFM_fab[0],
                            NC_PH_fab[0] , NC_PHB_fab[0],
                            NC_C1H_fab[0], NC_C2H_fab[0], NC_RDNW_fab[0],
                            NC_xvel_fab[0],NC_yvel_fab[0],NC_rho_fab[0],NC_rhoth_fab[0]);
        convert_wrfbdy_data(1,domain,bdy_data_xhi,
                            NC_MUB_fab[0], NC_MSFU_fab[0], NC_MSFV_fab[0], NC_MSFM_fab[0],
                            NC_PH_fab[0] , NC_PHB_fab[0],
                            NC_C1H_fab[0], NC_C2H_fab[0], NC_RDNW_fab[0],
                            NC_xvel_fab[0],NC_yvel_fab[0],NC_rho_fab[0],NC_rhoth_fab[0]);
        convert_wrfbdy_data(2,domain,bdy_data_ylo,
                            NC_MUB_fab[0], NC_MSFU_fab[0], NC_MSFV_fab[0], NC_MSFM_fab[0],
                            NC_PH_fab[0] , NC_PHB_fab[0],
                            NC_C1H_fab[0], NC_C2H_fab[0], NC_RDNW_fab[0],
                            NC_xvel_fab[0],NC_yvel_fab[0],NC_rho_fab[0],NC_rhoth_fab[0]);
        convert_wrfbdy_data(3,domain,bdy_data_yhi,
                            NC_MUB_fab[0], NC_MSFU_fab[0], NC_MSFV_fab[0], NC_MSFM_fab[0],
                            NC_PH_fab[0] , NC_PHB_fab[0],
                            NC_C1H_fab[0], NC_C2H_fab[0], NC_RDNW_fab[0],
                            NC_xvel_fab[0],NC_yvel_fab[0],NC_rho_fab[0],NC_rhoth_fab[0]);
    }
}

void
ROMSX::init_state_from_wrfinput(int lev, FArrayBox& state_fab,
                              FArrayBox& x_vel_fab, FArrayBox& y_vel_fab,
                              FArrayBox& z_vel_fab,
                              const Vector<FArrayBox>& NC_xvel_fab,
                              const Vector<FArrayBox>& NC_yvel_fab,
                              const Vector<FArrayBox>& NC_zvel_fab,
                              const Vector<FArrayBox>& NC_rho_fab,
                              const Vector<FArrayBox>& NC_temp_fab)
{
    for (int idx = 0; idx < num_boxes_at_level[lev]; idx++)
    {
        //
        // FArrayBox to FArrayBox copy does "copy on intersection"
        // This only works here because we have broadcast the FArrayBox of data from the netcdf file to all ranks
        //
        // This copies x-vel
        x_vel_fab.template copy<RunOn::Device>(NC_xvel_fab[idx]);

        // This copies y-vel
        y_vel_fab.template copy<RunOn::Device>(NC_yvel_fab[idx]);

        // This copies z-vel
        z_vel_fab.template copy<RunOn::Device>(NC_zvel_fab[idx]);

        // We first initialize all state_fab variables to zero
        state_fab.template setVal<RunOn::Device>(0.);

        // This copies the density
        state_fab.template copy<RunOn::Device>(NC_rho_fab[idx], 0, Rho_comp, 1);

        // This copies (rho*theta)
        state_fab.template copy<RunOn::Device>(NC_temp_fab[idx], 0, Temp_comp, 1);
    } // idx
}

void
ROMSX::init_msfs_from_wrfinput(int lev, FArrayBox& msfu_fab,
                             FArrayBox& msfv_fab, FArrayBox& msfm_fab,
                             const Vector<FArrayBox>& NC_MSFU_fab,
                             const Vector<FArrayBox>& NC_MSFV_fab,
                             const Vector<FArrayBox>& NC_MSFM_fab)
{
    for (int idx = 0; idx < num_boxes_at_level[lev]; idx++)
    {
        //
        // FArrayBox to FArrayBox copy does "copy on intersection"
        // This only works here because we have broadcast the FArrayBox of data from the netcdf file to all ranks
        //
        // This copies mapfac_u
        msfu_fab.template copy<RunOn::Device>(NC_MSFU_fab[idx]);

        // This copies mapfac_v
        msfv_fab.template copy<RunOn::Device>(NC_MSFV_fab[idx]);

        // This copies mapfac_m
        msfm_fab.template copy<RunOn::Device>(NC_MSFM_fab[idx]);
    } // idx
}

void
ROMSX::init_base_state_from_wrfinput(int lev, const Box& bx, FArrayBox& p_hse, FArrayBox& pi_hse,
                                   FArrayBox& r_hse, const FArrayBox& z_phys_nd_fab,
                                   const FArrayBox& z_phys_cc_fab,
                                   const Vector<FArrayBox>& NC_ALB_fab,
                                   const Vector<FArrayBox>& NC_PB_fab)
{
    for (int idx = 0; idx < num_boxes_at_level[lev]; idx++)
    {
        //
        // FArrayBox to FArrayBox copy does "copy on intersection"
        // This only works here because we have broadcast the FArrayBox of data from the netcdf file to all ranks
        //

        // This copies from NC_zphys on z-faces to z_phys_nd on nodes
        const Array4<Real      >&  p_hse_arr =  p_hse.array();
        const Array4<Real      >& pi_hse_arr = pi_hse.array();
        const Array4<Real      >&  r_hse_arr =  r_hse.array();
        const Array4<Real const>& alpha_arr = NC_ALB_fab[idx].const_array();
        const Array4<Real const>& nc_pb_arr = NC_PB_fab[idx].const_array();
        const Array4<Real const>&   z_nd_arr = z_phys_nd_fab.array();
        const Array4<Real const>&   z_cc_arr = z_phys_cc_fab.array();

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
            p_hse_arr(i,j,k)  = nc_pb_arr(i,j,k);
            pi_hse_arr(i,j,k) = getExnergivenP(p_hse_arr(i,j,k));
            r_hse_arr(i,j,k)  = 1.0 / alpha_arr(i,j,k);

        });
    } // idx
}

void
ROMSX::init_terrain_from_wrfinput(int lev, FArrayBox& z_phys,
                                const Vector<FArrayBox>& NC_PH_fab,
                                const Vector<FArrayBox>& NC_PHB_fab)
{
    for (int idx = 0; idx < num_boxes_at_level[lev]; idx++)
    {
        //
        // FArrayBox to FArrayBox copy does "copy on intersection"
        // This only works here because we have broadcast the FArrayBox of data from the netcdf file to all ranks
        //

        // This copies from NC_zphys on z-faces to z_phys_nd on nodes
        const Array4<Real      >&      z_arr = z_phys.array();
        const Array4<Real const>& nc_phb_arr = NC_PHB_fab[idx].const_array();
        const Array4<Real const>& nc_ph_arr  = NC_PH_fab[idx].const_array();

        const Box& z_phys_box(z_phys.box());

        Box nodal_box = amrex::surroundingNodes(NC_PHB_fab[idx].box());

        int ilo = nodal_box.smallEnd()[0];
        int ihi = nodal_box.bigEnd()[0];
        int jlo = nodal_box.smallEnd()[1];
        int jhi = nodal_box.bigEnd()[1];
        int klo = nodal_box.smallEnd()[2];
        int khi = nodal_box.bigEnd()[2];

        //
        // We must be careful not to read out of bounds of the WPS data
        //
        amrex::ParallelFor(z_phys_box, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
            int ii = std::max(std::min(i,ihi-1),ilo+1);
            int jj = std::max(std::min(j,jhi-1),jlo+1);
            if (k < 0) {
                Real z_klo   =  0.25 * ( nc_ph_arr (ii,jj  ,klo  ) +  nc_ph_arr(ii-1,jj  ,klo  ) +
                                         nc_ph_arr (ii,jj-1,klo  ) + nc_ph_arr (ii-1,jj-1,klo) +
                                         nc_phb_arr(ii,jj  ,klo  ) + nc_phb_arr(ii-1,jj  ,klo  ) +
                                         nc_phb_arr(ii,jj-1,klo  ) + nc_phb_arr(ii-1,jj-1,klo) ) / CONST_GRAV;
                Real z_klop1 =  0.25 * ( nc_ph_arr (ii,jj  ,klo+1) +  nc_ph_arr(ii-1,jj  ,klo+1) +
                                         nc_ph_arr (ii,jj-1,klo+1) + nc_ph_arr (ii-1,jj-1,klo+1) +
                                         nc_phb_arr(ii,jj  ,klo+1) + nc_phb_arr(ii-1,jj  ,klo+1) +
                                         nc_phb_arr(ii,jj-1,klo+1) + nc_phb_arr(ii-1,jj-1,klo+1) ) / CONST_GRAV;
                z_arr(i, j, k) = 2.0 * z_klo - z_klop1;
            } else if (k > khi) {
                Real z_khi   =  0.25 * ( nc_ph_arr (ii,jj  ,khi  ) + nc_ph_arr (ii-1,jj  ,khi  ) +
                                         nc_ph_arr (ii,jj-1,khi  ) + nc_ph_arr (ii-1,jj-1,khi) +
                                         nc_phb_arr(ii,jj  ,khi  ) + nc_phb_arr(ii-1,jj  ,khi  ) +
                                         nc_phb_arr(ii,jj-1,khi  ) + nc_phb_arr(ii-1,jj-1,khi) ) / CONST_GRAV;
                Real z_khim1 =  0.25 * ( nc_ph_arr (ii,jj  ,khi-1) + nc_ph_arr (ii-1,jj  ,khi-1) +
                                         nc_ph_arr (ii,jj-1,khi-1) + nc_ph_arr (ii-1,jj-1,khi-1) +
                                         nc_phb_arr(ii,jj  ,khi-1) + nc_phb_arr(ii-1,jj  ,khi-1) +
                                         nc_phb_arr(ii,jj-1,khi-1) + nc_phb_arr(ii-1,jj-1,khi-1) ) / CONST_GRAV;
                z_arr(i, j, k) = 2.0 * z_khi - z_khim1;
              } else {
                z_arr(i, j, k) = 0.25 * ( nc_ph_arr (ii,jj  ,k) + nc_ph_arr (ii-1,jj  ,k) +
                                          nc_ph_arr (ii,jj-1,k) + nc_ph_arr (ii-1,jj-1,k) +
                                          nc_phb_arr(ii,jj  ,k) + nc_phb_arr(ii-1,jj  ,k) +
                                          nc_phb_arr(ii,jj-1,k) + nc_phb_arr(ii-1,jj-1,k) ) / CONST_GRAV;
            } // k
        });
    } // idx
}
#endif // ROMSX_USE_NETCDF

void
ROMSX::init_custom(int lev)
{
    auto& lev_new = vars_new[lev];
    MultiFab r_hse(base_state[lev], make_alias, 0, 1); // r_0 is first  component
    MultiFab p_hse(base_state[lev], make_alias, 1, 1); // p_0 is second component
    std::unique_ptr<MultiFab>& mf_z_w = z_w[lev];
    std::unique_ptr<MultiFab>& mf_z_r = z_r[lev];
    std::unique_ptr<MultiFab>& mf_Hz  = Hz[lev];
    std::unique_ptr<MultiFab>& mf_h  = hOfTheConfusingName[lev];
    std::unique_ptr<MultiFab>& mf_Zt_avg1  = Zt_avg1[lev];

#ifdef _OPENMP
#pragma omp parallel if (amrex::Gpu::notInLaunchRegion())
#endif
    for (MFIter mfi(lev_new[Vars::cons], TilingIfNotGPU()); mfi.isValid(); ++mfi)
    {
        const Box &bx = mfi.tilebox();
        const auto &cons_arr = lev_new[Vars::cons].array(mfi);
        const auto &xvel_arr = lev_new[Vars::xvel].array(mfi);
        const auto &yvel_arr = lev_new[Vars::yvel].array(mfi);
        const auto &zvel_arr = lev_new[Vars::zvel].array(mfi);

        Array4<Real const> z_nd_arr = (solverChoice.use_terrain) ? z_phys_nd[lev]->const_array(mfi) : Array4<Real const>{};
        Array4<Real const> z_cc_arr = (solverChoice.use_terrain) ? z_phys_cc[lev]->const_array(mfi) : Array4<Real const>{};

        Array4<const Real> const& z_w_arr = (mf_z_w)->array(mfi);
        Array4<const Real> const& z_r_arr = (mf_z_r)->array(mfi);
        Array4<const Real> const& Hz_arr  = (mf_Hz)->array(mfi);
        Array4<const Real> const& h_arr  = (mf_h)->array(mfi);
        Array4<const Real> const& Zt_avg1_arr  = (mf_Zt_avg1)->array(mfi);

        Array4<Real> r_hse_arr = r_hse.array(mfi);
        Array4<Real> p_hse_arr = p_hse.array(mfi);

        init_custom_prob(bx, cons_arr, xvel_arr, yvel_arr, zvel_arr,
                         r_hse_arr, p_hse_arr, z_nd_arr, z_cc_arr,
                         z_w_arr, z_r_arr, Hz_arr, h_arr, Zt_avg1_arr, geom[lev].data());

    } //mfi
}
