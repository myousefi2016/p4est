/*
  This file is part of p4est.
  p4est is a C library to manage a collection (a forest) of multiple
  connected adaptive quadtrees or octrees in parallel.

  Copyright (C) 2012 Carsten Burstedde

  p4est is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  p4est is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with p4est; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#ifndef P4EST_WRAP_H
#define P4EST_WRAP_H

/** \file p4est_wrap.h
 * The logic in p4est_wrap encapsulates core p4est data structures and provides
 * functions that clarify the mark-adapt-partition cycle.  There is also an
 * element iterator that can replace the nested loops over trees and tree
 * quadrants, respectively, which can help make application code cleaner.
 */

#include <p4est_mesh.h>
#include <p4est_extended.h>

SC_EXTERN_C_BEGIN;

/*** COMPLETE INTERNAL STATE OF P4EST ***/

typedef enum p4est_wrap_flags
{
  P4EST_WRAP_NONE = 0,
  P4EST_WRAP_REFINE = 0x01,
  P4EST_WRAP_COARSEN = 0x02
}
p4est_wrap_flags_t;

typedef struct p4est_wrap
{
  /* this member is never used or changed by p4est_wrap */
  void               *user_pointer;     /**< Convenience member for users */

  /** If true, this wrap has NULL for ghost, mesh, and flag members.
   * If false, they are properly allocated and kept current internally. */
  int                 hollow;

  /** Non-negative integer tells us how many adaptations to wait
   * before any given quadrent may be coarsened again. */
  int                 coarsen_delay;

  /** Boolean: If true, we delay coarsening not only after refinement,
   * but also between subsequent coarsenings of the same quadrant. */
  int                 coarsen_affect;

  /* these members are considered public and read-only */
  int                 p4est_dim;
  int                 p4est_half;
  int                 p4est_faces;
  int                 p4est_children;
  p4est_connect_type_t btype;
  p4est_replace_t     replace_fn;
  p4est_connectivity_t *conn;
  p4est_t            *p4est;    /**< p4est->user_pointer is used internally */

  /* anything below here is considered private und should not be touched */
  int                 weight_exponent;
  uint8_t            *flags, *temp_flags;
  p4est_locidx_t      num_refine_flags, inside_counter, num_replaced;

  /* for ghost and mesh use p4est_wrap_get_ghost, _mesh declared below */
  p4est_ghost_t      *ghost;
  p4est_mesh_t       *mesh;
  p4est_ghost_t      *ghost_aux;
  p4est_mesh_t       *mesh_aux;
  int                 match_aux;
}
p4est_wrap_t;

/** Create a p4est wrapper from a given connectivity structure.
 * The ghost and mesh members are initialized as well as the flags.
 * The btype is set to P4EST_CONNECT_FULL.
 * \param [in] mpicomm        We expect sc_MPI_Init to be called already.
 * \param [in] conn           Connectivity structure.  Wrap takes ownership.
 * \param [in] initial_level  Initial level of uniform refinement.
 * \return                    A fully initialized p4est_wrap structure.
 */
p4est_wrap_t       *p4est_wrap_new_conn (sc_MPI_Comm mpicomm,
                                         p4est_connectivity_t * conn,
                                         int initial_level);

/** Create a p4est wrapper from a given connectivity structure.
 * Like p4est_wrap_new_conn, but with extra parameters \a hollow and \a btype.
 * \param [in] mpicomm        We expect sc_MPI_Init to be called already.
 * \param [in] conn           Connectivity structure.  Wrap takes ownership.
 * \param [in] initial_level  Initial level of uniform refinement.
 *                            No effect if less/equal to zero.
 * \param [in] hollow         Do not allocate flags, ghost, and mesh members.
 * \param [in] btype          The neighborhood used for balance, ghost, mesh.
 * \param [in] replace_fn     Callback to replace quadrants during refinement,
 *                            coarsening or balancing in p4est_wrap_adapt.
 *                            May be NULL.
 * \param [in] user_pointer   Set the user pointer in p4est_wrap_t.
 *                            Subsequently, we will never access it.
 * \return                    A fully initialized p4est_wrap structure.
 */
p4est_wrap_t       *p4est_wrap_new_ext (sc_MPI_Comm mpicomm,
                                        p4est_connectivity_t * conn,
                                        int initial_level, int hollow,
                                        p4est_connect_type_t btype,
                                        p4est_replace_t replace_fn,
                                        void *user_pointer);

/** Create p4est and auxiliary data structures.
 * Expects sc_MPI_Init to be called beforehand.
 */
p4est_wrap_t       *p4est_wrap_new_unitsquare (sc_MPI_Comm mpicomm,
                                               int initial_level);
p4est_wrap_t       *p4est_wrap_new_periodic (sc_MPI_Comm mpicomm,
                                             int initial_level);
p4est_wrap_t       *p4est_wrap_new_rotwrap (sc_MPI_Comm mpicomm,
                                            int initial_level);
p4est_wrap_t       *p4est_wrap_new_corner (sc_MPI_Comm mpicomm,
                                           int initial_level);
p4est_wrap_t       *p4est_wrap_new_pillow (sc_MPI_Comm mpicomm,
                                           int initial_level);
p4est_wrap_t       *p4est_wrap_new_moebius (sc_MPI_Comm mpicomm,
                                            int initial_level);
p4est_wrap_t       *p4est_wrap_new_cubed (sc_MPI_Comm mpicomm,
                                          int initial_level);
p4est_wrap_t       *p4est_wrap_new_disk (sc_MPI_Comm mpicomm,
                                         int initial_level);
p4est_wrap_t       *p4est_wrap_new_brick (sc_MPI_Comm mpicomm,
                                          int bx, int by, int px, int py,
                                          int initial_level);

/** Passes sc_MPI_COMM_WORLD to p4est_wrap_new_unitsquare. */
p4est_wrap_t       *p4est_wrap_new_world (int initial_level);
void                p4est_wrap_destroy (p4est_wrap_t * pp);

/** Change hollow status of the wrap.
 * A wrap is hollow if the flags, ghost, and mesh members are NULL.
 * Legal to set to current hollow status, in which case wrap is not changed.
 * If changed from not hollow to hollow, previously set refinement and
 * coarsening flags are zeroed.
 * \param [in,out] pp   The present wrap structure, hollow or not.
 * \param [in] hollow   The desired hollow status.  If set to hollow,
 *                      refinement flags are zeroed.
 */
void                p4est_wrap_set_hollow (p4est_wrap_t * pp, int hollow);

/** Set a parameter that delays coarsening after adaptation.
 * If positive each quadrant counts the number of adaptations it has survived.
 * Calling this function initializes all quadrant counters to zero.
 * On adaptation we only coarsen a quadrant if it is old enough.
 * Optionally, we can also delay the time between subsequent coarsenings.
 * \param [in,out] pp           A valid p4est_wrap structure.
 * \param [in] coarsen_delay    Set how many adaptation cycles a quadrant has
 *                              to wait to be allowed to coarsen.
 *                              Non-negative number; 0 disables the feature.
 *                              Suggested default value: not larger than 2.
 * \param [in] coarsen_affect   Boolean; If true, we not only count from the
 *                              most recent refinement but also between
 *                              subsequent coarsenings.
 *                              Suggested default: 0.
 */
void                p4est_wrap_set_coarsen_delay (p4est_wrap_t * pp,
                                                  int coarsen_delay,
                                                  int coarsen_affect);

/** Return the appropriate ghost layer.
 * This function is necessary since two versions may exist simultaneously
 * after refinement and before partition/complete.
 * \param [in] pp   Must have !pp->hollow.
 * */
p4est_ghost_t      *p4est_wrap_get_ghost (p4est_wrap_t * pp);

/** Return the appropriate mesh structure.
 * This function is necessary since two versions may exist simultaneously
 * after refinement and before partition/complete.
 * \param [in] pp   Must have !pp->hollow.
 * */
p4est_mesh_t       *p4est_wrap_get_mesh (p4est_wrap_t * pp);

/** Mark a local element for refinement.
 * This will cancel any coarsening mark set previously for this element.
 * \param [in,out] pp The p4est wrapper to work with, must not be hollow.
 * \param [in] which_tree The number of the tree this element lives in.
 * \param [in] which_quad The number of this element relative to its tree.
 */
void                p4est_wrap_mark_refine (p4est_wrap_t * pp,
                                            p4est_topidx_t which_tree,
                                            p4est_locidx_t which_quad);

/** Mark a local element for coarsening.
 * This will cancel any refinement mark set previously for this element.
 * \param [in,out] pp The p4est wrapper to work with, must not be hollow.
 * \param [in] which_tree The number of the tree this element lives in.
 * \param [in] which_quad The number of this element relative to its tree.
 */
void                p4est_wrap_mark_coarsen (p4est_wrap_t * pp,
                                             p4est_topidx_t which_tree,
                                             p4est_locidx_t which_quad);

/** Call p4est_refine, coarsen, and balance to update pp->p4est.
 * Checks pp->flags as per-quadrant input against p4est_wrap_flags_t.
 * The pp->flags array is updated along with p4est and reset to zeros.
 * Creates ghost_aux and mesh_aux to represent the intermediate mesh.
 * \param [in,out] pp The p4est wrapper to work with, must not be hollow.
 * \return          boolean whether p4est has changed.
 *                  If true, partition must be called.
 *                  If false, partition must not be called, and
 *                  complete must not be called either.
 */
int                 p4est_wrap_adapt (p4est_wrap_t * pp);

/** Call p4est_partition for equal leaf distribution.
 * Frees the old ghost and mesh first and updates pp->flags along with p4est.
 * The pp->flags array is reset to zeros.
 * Creates ghost and mesh to represent the new mesh.
 * \param [in,out] pp The p4est wrapper to work with, must not be hollow.
 * \param [in] weight_exponent      Integer weight assigned to each leaf
 *                  according to 2 ** (level * exponent).  Passing 0 assigns
 *                  equal weight to all leaves.  Passing 1 increases the
 *                  leaf weight by a factor of two for each level increase.
 *                  CURRENTLY ONLY 0 AND 1 ARE LEGAL VALUES.
 * \return          boolean whether p4est has changed.
 *                  If true, complete must be called.
 *                  If false, complete must not be called.
 */
int                 p4est_wrap_partition (p4est_wrap_t * pp,
                                          int weight_exponent);

/** Free memory for the intermediate mesh.
 * Sets mesh_aux and ghost_aux to NULL.
 * This function must be used if both refinement and partition effect changes.
 * After this call, we are ready for another mark-refine-partition cycle.
 * \param [in,out] pp The p4est wrapper to work with, must not be hollow.
 */
void                p4est_wrap_complete (p4est_wrap_t * pp);

/*** ITERATOR OVER THE FOREST LEAVES ***/

typedef struct p4est_wrap_leaf
{
  p4est_wrap_t       *pp;             /**< Must contain a valid ghost */

  /* Information about the current quadrant */
  p4est_topidx_t      which_tree;     /**< Current tree number */
  p4est_locidx_t      which_quad;     /**< Quadrant number relative to tree */
  p4est_locidx_t      local_quad;     /**< Quadrant number relative to proc */
  p4est_tree_t       *tree;           /**< Current tree */
  sc_array_t         *tquadrants;     /**< Current tree's quadrants */
  p4est_quadrant_t   *quad;           /**< Current quadrant */
#if 0                           /* DEPRECATED -- anyone using them? */
  int                 level;
  double              lowerleft[3];
  double              upperright[3];
#endif

  /* Information about parallel neighbors */
  int                 is_mirror;      /**< Quadrant at parallel boundary? */
  sc_array_t         *mirrors;        /**< If not NULL, from pp's ghost */
  p4est_locidx_t      nm;             /**< Internal: mirror counter */
  p4est_locidx_t      next_mirror_quadrant;     /**< Internal: next */
}
p4est_wrap_leaf_t;

/** Determine whether we have just entered a different tree */
#define P4EST_LEAF_IS_FIRST_IN_TREE(wleaf) ((wleaf)->which_quad == 0)

/* Create an iterator over the local leaves in the forest.
 * Returns a newly allocated state containing the first leaf,
 * or NULL if the local partition of the tree is empty.
 * \param [in] pp   Legal p4est_wrap structure, hollow or not.
 * \param [in] track_mirrors    If true, \a pp must not be hollow and mirror
 *                              information from the ghost layer is stored.
 * \return          NULL if processor is empty, otherwise a leaf iterator for
 *                  subsequent use with \a p4est_wrap_leaf_next.
 */
p4est_wrap_leaf_t  *p4est_wrap_leaf_first (p4est_wrap_t * pp,
                                           int track_mirrors);

/* Move the forest leaf iterator forward.
 * \param [in,out] leaf     A non-NULL leaf iterator created by
 *                          \ref p4est_wrap_leaf_first.
 * \return          The state that was input with updated information for the
 *                  next leaf, or NULL and deallocates the input if called with
 *                  the last leaf on this processor.
 */
p4est_wrap_leaf_t  *p4est_wrap_leaf_next (p4est_wrap_leaf_t * leaf);

SC_EXTERN_C_END;

#endif /* !P4EST_WRAP_H */
