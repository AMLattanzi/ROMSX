#include "AMReX_PhysBCFunct.H"
#include <REMORA_PhysBCFunct.H>

using namespace amrex;

//
// dest_arr is the Array4 to be filled
// time is the time at which the data should be filled
// bccomp is the index into both domain_bcs_type_bcr and bc_extdir_vals
//     so this follows the BCVars enum
//
void REMORAPhysBCFunct::impose_yvel_bcs (const Array4<Real>& dest_arr, const Box& bx, const Box& domain,
                                        const GpuArray<Real,AMREX_SPACEDIM> /*dxInv*/, const Array4<const Real>& mskv,
                                        const Array4<const Real>& calc_arr,
                                        Real /*time*/, int bccomp)
{
    BL_PROFILE_VAR("impose_yvel_bcs()",impose_yvel_bcs);
    const auto& dom_lo = amrex::lbound(domain);
    const auto& dom_hi = amrex::ubound(domain);

    // Based on BCRec for the domain, we need to make BCRec for this Box
    // bccomp is used as starting index for m_domain_bcs_type
    //      0 is used as starting index for bcrs
    int ncomp = 1;
    Vector<BCRec> bcrs(ncomp);
    amrex::setBC(bx, domain, bccomp, 0, ncomp, m_domain_bcs_type, bcrs);

    // xlo: ori = 0
    // ylo: ori = 1
    // zlo: ori = 2
    // xhi: ori = 3
    // yhi: ori = 4
    // zhi: ori = 5

    amrex::Gpu::DeviceVector<BCRec> bcrs_d(ncomp);
#ifdef AMREX_USE_GPU
    Gpu::htod_memcpy_async(bcrs_d.data(), bcrs.data(), sizeof(BCRec)*ncomp);
#else
    std::memcpy(bcrs_d.data(), bcrs.data(), sizeof(BCRec)*ncomp);
#endif
    const amrex::BCRec* bc_ptr = bcrs_d.data();

    GpuArray<GpuArray<Real, AMREX_SPACEDIM*2>, AMREX_SPACEDIM+NCONS+8> l_bc_extdir_vals_d;

    for (int i = 0; i < ncomp; i++)
        for (int ori = 0; ori < 2*AMREX_SPACEDIM; ori++)
            l_bc_extdir_vals_d[i][ori] = m_bc_extdir_vals[bccomp+i][ori];

    GeometryData const& geomdata = m_geom.data();
    bool is_periodic_in_x = geomdata.isPeriodic(0);
    bool is_periodic_in_y = geomdata.isPeriodic(1);
    const Real eps= 1.0e-20_rt;

    // First do all ext_dir bcs
    if (!is_periodic_in_x or bccomp==BCVars::foextrap_bc)
    {
        // Populate ghost cells on lo-x and hi-x domain boundaries
        Box bx_xlo(bx);  bx_xlo.setBig  (0,dom_lo.x-1);
        Box bx_xhi(bx);  bx_xhi.setSmall(0,dom_hi.x+1);
        ParallelFor(
            grow(bx_xlo,IntVect(0,-1,0)), ncomp, [=] AMREX_GPU_DEVICE (int i, int j, int k, int n) {
                int iflip = dom_lo.x - 1- i;
                if (bc_ptr[n].lo(0) == REMORABCType::ext_dir) {
                    dest_arr(i,j,k) = l_bc_extdir_vals_d[n][0]*mskv(i,j,0);
                } else if (bc_ptr[n].lo(0) == REMORABCType::foextrap || bc_ptr[n].lo(0) == REMORABCType::clamped) {
                    dest_arr(i,j,k) =  dest_arr(dom_lo.x,j,k)*mskv(i,j,0);
                } else if (bc_ptr[n].lo(0) == REMORABCType::orlanski_rad) {
                    Real grad_lo_im1  = calc_arr(dom_lo.x-1,j+1,k) - calc_arr(dom_lo.x-1,j  ,k);
                    Real grad_lo      = calc_arr(dom_lo.x  ,j+1,k) - calc_arr(dom_lo.x  ,j  ,k);
                    Real grad_lo_ijm1 = calc_arr(dom_lo.x-1,j  ,k) - calc_arr(dom_lo.x-1,j-1,k);
                    Real grad_lo_jm1  = calc_arr(dom_lo.x  ,j  ,k) - calc_arr(dom_lo.x  ,j-1,k);
                    Real dVdt = calc_arr(dom_lo.x,j,k) - dest_arr(dom_lo.x  ,j,k);
                    Real dVdx = dest_arr(dom_lo.x,j,k) - dest_arr(dom_lo.x+1,j,k);
                    if (dVdt * dVdx < 0.0_rt) dVdt = 0.0_rt;
                    Real dVde = (dVdt * (grad_lo_jm1 + grad_lo) > 0.0_rt) ? grad_lo_jm1 : grad_lo;
                    Real cff = std::max(dVdx*dVdx + dVde*dVde,eps);
                    Real Cx = dVdt * dVdx;
                    dest_arr(i,j,k) = (cff * calc_arr(dom_lo.x-1,j,k) + Cx * dest_arr(dom_lo.x,j,k)) * mskv(i,j,0) / (cff + Cx);
                } else if (bc_ptr[n].lo(0) == REMORABCType::reflect_even) {
                    dest_arr(i,j,k) =  dest_arr(iflip,j,k)*mskv(i,j,0);
                } else if (bc_ptr[n].lo(0) == REMORABCType::reflect_odd) {
                    dest_arr(i,j,k) = -dest_arr(iflip,j,k)*mskv(i,j,0);
                }
            },
            grow(bx_xhi,IntVect(0,-1,0)), ncomp, [=] AMREX_GPU_DEVICE (int i, int j, int k, int n) {
                int iflip =  2*dom_hi.x + 1 - i;
                if (bc_ptr[n].hi(0) == REMORABCType::ext_dir) {
                    dest_arr(i,j,k) = l_bc_extdir_vals_d[n][3]*mskv(i,j,0);
                } else if (bc_ptr[n].hi(0) == REMORABCType::foextrap || bc_ptr[n].hi(0) == REMORABCType::clamped) {
                    dest_arr(i,j,k) =  dest_arr(dom_hi.x,j,k)*mskv(i,j,0);
                } else if (bc_ptr[n].hi(0) == REMORABCType::orlanski_rad) {
                    Real grad_hi        = calc_arr(dom_hi.x  ,j+1,k) - calc_arr(dom_hi.x  ,j  ,k);
                    Real grad_hi_ip1    = calc_arr(dom_hi.x+1,j+1,k) - calc_arr(dom_hi.x+1,j  ,k);
                    Real grad_hi_jm1    = calc_arr(dom_hi.x  ,j  ,k) - calc_arr(dom_hi.x  ,j-1,k);
                    Real grad_hi_ipjm1  = calc_arr(dom_hi.x+1,j  ,k) - calc_arr(dom_hi.x+1,j-1,k);
                    Real dVdt = calc_arr(dom_hi.x,j,k) - dest_arr(dom_hi.x  ,j,k);
                    Real dVdx = dest_arr(dom_hi.x,j,k) - dest_arr(dom_hi.x-1,j,k);
                    if (dVdt*dVdx < 0.0_rt) dVdt = 0.0_rt;
                    Real dVde = (dVdt * (grad_hi_jm1 + grad_hi) > 0.0_rt) ? grad_hi_jm1 : grad_hi;
                    Real cff = std::max(dVdx*dVdx+dVde*dVde,eps);
                    Real Cx = dVdt * dVdx;
                    dest_arr(i,j,k) = (cff * calc_arr(dom_hi.x+1,j,k) + Cx * dest_arr(dom_hi.x,j,k)) * mskv(i,j,0) / (cff + Cx);
                } else if (bc_ptr[n].hi(0) == REMORABCType::reflect_even) {
                    dest_arr(i,j,k) =  dest_arr(iflip,j,k)*mskv(i,j,0);
                } else if (bc_ptr[n].hi(0) == REMORABCType::reflect_odd) {
                    dest_arr(i,j,k) = -dest_arr(iflip,j,k)*mskv(i,j,0);
                }
            }
        );
    }

    if (!is_periodic_in_y or bccomp==BCVars::foextrap_bc)
    {
        // Populate ghost cells on lo-y and hi-y domain boundaries
        Box bx_ylo(bx);  bx_ylo.setBig  (1,dom_lo.y-1);
        Box bx_yhi(bx);  bx_yhi.setSmall(1,dom_hi.y+2);
        Box bx_ylo_face(bx); bx_ylo_face.setSmall(1,dom_lo.y  ); bx_ylo_face.setBig(1,dom_lo.y  );
        Box bx_yhi_face(bx); bx_yhi_face.setSmall(1,dom_hi.y+1); bx_yhi_face.setBig(1,dom_hi.y+1);

        ParallelFor(
            // We only set the values on the domain faces themselves if EXT_DIR or outflow
            grow(bx_ylo_face,IntVect(-1,0,0)), ncomp, [=] AMREX_GPU_DEVICE (int i, int j, int k, int n) {
                if (bc_ptr[n].lo(1) == REMORABCType::ext_dir) {
                    dest_arr(i,j,k) = l_bc_extdir_vals_d[n][1]*mskv(i,j,0);
                } else if (bc_ptr[n].lo(1) == REMORABCType::foextrap) {
                    dest_arr(i,j,k) =  dest_arr(i,dom_lo.y+1,k)*mskv(i,j,0);
                } else if (bc_ptr[n].lo(1) == REMORABCType::orlanski_rad) {
                    Real grad_lo      = calc_arr(i  ,dom_lo.y  ,k) - calc_arr(i-1,dom_lo.y  ,k);
                    Real grad_lo_jp1  = calc_arr(i  ,dom_lo.y+1,k) - calc_arr(i-1,dom_lo.y+1,k);
                    Real grad_lo_ip1  = calc_arr(i+1,dom_lo.y  ,k) - calc_arr(i  ,dom_lo.y  ,k);
                    Real grad_lo_ijp1 = calc_arr(i+1,dom_lo.y+1,k) - calc_arr(i  ,dom_lo.y+1,k);
                    Real dVdt = calc_arr(i,dom_lo.y+1,k) - dest_arr(i,dom_lo.y+1,k);
                    Real dVde = dest_arr(i,dom_lo.y+1,k) - dest_arr(i,dom_lo.y+2,k);
                    if (dVdt*dVde < 0.0_rt) dVdt = 0.0_rt;
                    Real dVdx = (dVdt * (grad_lo_jp1 + grad_lo_ijp1) > 0.0_rt) ? grad_lo_jp1 : grad_lo_ijp1;
                    Real cff = std::max(dVdx*dVdx + dVde*dVde, eps);
                    Real Ce = dVdt * dVde;
                    dest_arr(i,j,k) = (cff * calc_arr(i,dom_lo.y,k) + Ce * dest_arr(i,dom_lo.y+1,k)) * mskv(i,j,0) / (cff + Ce);
                }
            });
        ParallelFor(
            grow(bx_ylo,IntVect(-1,0,0)), ncomp, [=] AMREX_GPU_DEVICE (int i, int j, int k, int n) {
                int jflip = dom_lo.y-j;
                int inner = (bc_ptr[n].lo(1) == REMORABCType::foextrap) ? 1 : 0;
                if (bc_ptr[n].lo(1) == REMORABCType::ext_dir) {
                    dest_arr(i,j,k) = l_bc_extdir_vals_d[n][1]*mskv(i,j,0);
                } else if (bc_ptr[n].lo(1) == REMORABCType::foextrap || bc_ptr[n].lo(1) == REMORABCType::clamped ||
                        bc_ptr[n].lo(1) == REMORABCType::orlanski_rad) {
                    dest_arr(i,j,k) =  dest_arr(i,dom_lo.y+inner,k)*mskv(i,j,0);
                } else if (bc_ptr[n].lo(1) == REMORABCType::reflect_even) {
                    dest_arr(i,j,k) =  dest_arr(i,jflip,k)*mskv(i,j,0);
                } else if (bc_ptr[n].lo(1) == REMORABCType::reflect_odd) {
                    dest_arr(i,j,k) = -dest_arr(i,jflip,k)*mskv(i,j,0);
                }
            });
        ParallelFor(
            // We only set the values on the domain faces themselves if EXT_DIR or outflow
            grow(bx_yhi_face,IntVect(-1,0,0)), ncomp, [=] AMREX_GPU_DEVICE (int i, int j, int k, int n) {
                if (bc_ptr[n].hi(1) == REMORABCType::ext_dir) {
                    dest_arr(i,j,k) = l_bc_extdir_vals_d[n][4]*mskv(i,j,0);
                } else if (bc_ptr[n].hi(1) == REMORABCType::foextrap) {
                    dest_arr(i,j,k) =  dest_arr(i,dom_hi.y,k)*mskv(i,j,0);
                 } else if (bc_ptr[n].hi(1) == REMORABCType::orlanski_rad) {
                    Real grad_hi      = calc_arr(i  ,dom_hi.y  ,k) - calc_arr(i-1,dom_hi.y  ,k);
                    Real grad_hi_jp1  = calc_arr(i  ,dom_hi.y+1,k) - calc_arr(i-1,dom_hi.y+1,k);
                    Real grad_hi_ip1  = calc_arr(i+1,dom_hi.y  ,k) - calc_arr(i  ,dom_hi.y  ,k);
                    Real grad_hi_ijp1 = calc_arr(i+1,dom_hi.y+1,k) - calc_arr(i  ,dom_hi.y+1,k);
                    Real dVdt = calc_arr(i,dom_hi.y,k) - dest_arr(i,dom_hi.y  ,k);
                    Real dVde = dest_arr(i,dom_hi.y,k) - dest_arr(i,dom_hi.y-1,k);
                    if (dVdt*dVde < 0.0_rt) dVdt = 0.0_rt;
                    Real dVdx = (dVdt * (grad_hi + grad_hi_ip1) > 0.0_rt) ? grad_hi : grad_hi_ip1;
                    Real cff = std::max(dVdx*dVdx + dVde*dVde, eps);
                    Real Ce = dVdt * dVde;
                    dest_arr(i,j,k) = (cff * calc_arr(i,dom_hi.y+1,k) + Ce * dest_arr(i,dom_hi.y,k)) * mskv(i,j,0) / (cff + Ce);
                }
            });
        ParallelFor(
            grow(bx_yhi,IntVect(-1,0,0)), ncomp, [=] AMREX_GPU_DEVICE (int i, int j, int k, int n) {
                 int jflip =  2*(dom_hi.y + 1) - j;
                 int inner = (bc_ptr[n].hi(1) == REMORABCType::foextrap) ? 1 : 0;
                 if (bc_ptr[n].hi(1) == REMORABCType::ext_dir) {
                     dest_arr(i,j,k) = l_bc_extdir_vals_d[n][4]*mskv(i,j,0);
                 } else if (bc_ptr[n].hi(1) == REMORABCType::foextrap || bc_ptr[n].hi(1) == REMORABCType::clamped ||
                         bc_ptr[n].hi(1) == REMORABCType::orlanski_rad) {
                     dest_arr(i,j,k) =  dest_arr(i,dom_hi.y+1-inner,k)*mskv(i,j,0);
                 } else if (bc_ptr[n].hi(1) == REMORABCType::reflect_even) {
                     dest_arr(i,j,k) =  dest_arr(i,jflip,k)*mskv(i,j,0);
                 } else if (bc_ptr[n].hi(1) == REMORABCType::reflect_odd) {
                     dest_arr(i,j,k) = -dest_arr(i,jflip,k)*mskv(i,j,0);
                }
            });
    }

    {
        // Populate ghost cells on lo-z and hi-z domain boundaries
        Box bx_zlo(bx);  bx_zlo.setBig  (2,dom_lo.z-1);
        Box bx_zhi(bx);  bx_zhi.setSmall(2,dom_hi.z+1);
        ParallelFor(
            bx_zlo, ncomp, [=] AMREX_GPU_DEVICE (int i, int j, int k, int n) {
                int kflip = dom_lo.z - 1 - k;
                if (bc_ptr[n].lo(2) == REMORABCType::ext_dir) {
                    dest_arr(i,j,k) = l_bc_extdir_vals_d[n][2]*mskv(i,j,0);
                } else if (bc_ptr[n].lo(2) == REMORABCType::foextrap) {
                    dest_arr(i,j,k) =  dest_arr(i,j,dom_lo.z)*mskv(i,j,0);
                } else if (bc_ptr[n].lo(2) == REMORABCType::reflect_even) {
                    dest_arr(i,j,k) =  dest_arr(i,j,kflip)*mskv(i,j,0);
                } else if (bc_ptr[n].lo(2) == REMORABCType::reflect_odd) {
                    dest_arr(i,j,k) = -dest_arr(i,j,kflip)*mskv(i,j,0);
                }
            },
            bx_zhi, ncomp, [=] AMREX_GPU_DEVICE (int i, int j, int k, int n) {
                int kflip =  2*dom_hi.z + 1 - k;
                if (bc_ptr[n].hi(2) == REMORABCType::ext_dir) {
                    dest_arr(i,j,k) = l_bc_extdir_vals_d[n][5]*mskv(i,j,0);
                } else if (bc_ptr[n].hi(2) == REMORABCType::foextrap) {
                    dest_arr(i,j,k) =  dest_arr(i,j,dom_hi.z)*mskv(i,j,0);
                } else if (bc_ptr[n].hi(2) == REMORABCType::reflect_even) {
                    dest_arr(i,j,k) =  dest_arr(i,j,kflip)*mskv(i,j,0);
                } else if (bc_ptr[n].hi(2) == REMORABCType::reflect_odd) {
                    dest_arr(i,j,k) = -dest_arr(i,j,kflip)*mskv(i,j,0);
                }
            }
        );
    }

    if ((!is_periodic_in_x or bccomp==BCVars::foextrap_bc) and
        (!is_periodic_in_y or bccomp==BCVars::foextrap_bc)) {
        Box xlo(bx);  xlo.setBig  (0,dom_lo.x-1);
        Box xhi(bx);  xhi.setSmall(0,dom_hi.x+1);
        Box ylo(bx);  ylo.setBig  (1,dom_lo.y  );
        Box yhi(bx);  yhi.setSmall(1,dom_hi.y+1);
        Box xlo_ylo = xlo & ylo;
        Box xlo_yhi = xlo & yhi;
        Box xhi_ylo = xhi & ylo;
        Box xhi_yhi = xhi & yhi;
        const bool clamp_west = m_domain_bcs_type[bccomp].lo(0) == REMORABCType::clamped;
        const bool clamp_east = m_domain_bcs_type[bccomp].hi(0) == REMORABCType::clamped;
        const bool clamp_south = m_domain_bcs_type[bccomp].lo(1) == REMORABCType::clamped;
        const bool clamp_north = m_domain_bcs_type[bccomp].hi(1) == REMORABCType::clamped;

        if (!clamp_west && !clamp_south) {
            ParallelFor(xlo_ylo, [=] AMREX_GPU_DEVICE (int i, int j, int k)
            {
                dest_arr(i,j,k) = 0.5 * (dest_arr(i,dom_lo.y+1,k) + dest_arr(dom_lo.x,j,k));
            });
        }
        if (!clamp_west && !clamp_north) {
            ParallelFor(xlo_yhi, [=] AMREX_GPU_DEVICE (int i, int j, int k)
            {
                dest_arr(i,j,k) = 0.5 * (dest_arr(i,dom_hi.y,k) + dest_arr(dom_lo.x,j,k));
            });
        }
        if (!clamp_east && !clamp_south) {
            ParallelFor(xhi_ylo, [=] AMREX_GPU_DEVICE (int i, int j, int k)
            {
                dest_arr(i,j,k) = 0.5 * (dest_arr(i,dom_lo.y+1,k) + dest_arr(dom_hi.x,j,k));
            });
        }
        if (!clamp_east && !clamp_north) {
            ParallelFor(xhi_yhi, [=] AMREX_GPU_DEVICE (int i, int j, int k)
            {
                dest_arr(i,j,k) = 0.5 * (dest_arr(i,dom_hi.y,k) + dest_arr(dom_hi.x,j,k));
            });
        }
    }

    Gpu::streamSynchronize();
}
