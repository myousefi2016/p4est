/*
  This file is part of p4est.
  p4est is a C library to manage a collection (a forest) of multiple
  connected adaptive quadtrees or octrees in parallel.

  Copyright (C) 2010 The University of Texas System
  Written by Carsten Burstedde, Lucas C. Wilcox, and Tobin Isaac

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

#ifdef P4_TO_P8
#include <p8est_algorithms.h>
#include <p8est_bits.h>
#include <p8est_communication.h>
#include <p8est_search.h>
#include <p8est_balance.h>
#else
#include <p4est_algorithms.h>
#include <p4est_bits.h>
#include <p4est_communication.h>
#include <p4est_search.h>
#include <p4est_balance.h>
#endif /* !P4_TO_P8 */

/* htonl is in either of these two */
#ifdef P4EST_HAVE_ARPA_NET_H
#include <arpa/inet.h>
#endif
#ifdef P4EST_HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifndef P4_TO_P8

/* *INDENT-OFF* */

/** Store the number of quadrants to add for complete and balance stages. */
static const int    p4est_balance_count[P4EST_DIM + 1] =
{ 5, 7, 8 };

/** Store coordinates of quadrants to add for balancing. */
static const p4est_qcoord_t p4est_balance_coord[8][P4EST_DIM] =
{ /* faces */
  { -1,  1 },
  {  2,  0 },
  {  1, -1 },
  {  0,  2 },
  /* corners */
  { -1, -1 },
  {  2, -1 },
  { -1,  2 },
  {  2,  2 }};

/** Offset for corners into p4est_balance_coord */
static const int    pbco = P4EST_FACES;

/* *INDENT-ON* */

#endif /* !P4_TO_P8 */

void
p4est_quadrant_init_data (p4est_t * p4est, p4est_topidx_t which_tree,
                          p4est_quadrant_t * quad, p4est_init_t init_fn)
{
  P4EST_ASSERT (p4est_quadrant_is_extended (quad));

  if (p4est->data_size > 0) {
    quad->p.user_data = sc_mempool_alloc (p4est->user_data_pool);
  }
  else {
    quad->p.user_data = NULL;
  }
  if (init_fn != NULL && p4est_quadrant_is_inside_root (quad)) {
    init_fn (p4est, which_tree, quad);
  }
}

void
p4est_quadrant_free_data (p4est_t * p4est, p4est_quadrant_t * quad)
{
  P4EST_ASSERT (p4est_quadrant_is_extended (quad));

  if (p4est->data_size > 0) {
    sc_mempool_free (p4est->user_data_pool, quad->p.user_data);
  }
  quad->p.user_data = NULL;
}

unsigned
p4est_quadrant_checksum (sc_array_t * quadrants,
                         sc_array_t * checkarray, size_t first_quadrant)
{
  int                 own_check;
  size_t              kz, qcount;
  unsigned            crc;
  uint32_t           *check;
  p4est_quadrant_t   *q;

  qcount = quadrants->elem_count;

  P4EST_ASSERT (quadrants->elem_size == sizeof (p4est_quadrant_t));
  P4EST_ASSERT (first_quadrant <= qcount);

  if (checkarray == NULL) {
    checkarray = sc_array_new (4);
    own_check = 1;
  }
  else {
    P4EST_ASSERT (checkarray->elem_size == 4);
    own_check = 0;
  }

  sc_array_resize (checkarray, (qcount - first_quadrant) * (P4EST_DIM + 1));
  for (kz = first_quadrant; kz < qcount; ++kz) {
    q = p4est_quadrant_array_index (quadrants, kz);
    P4EST_ASSERT (p4est_quadrant_is_extended (q));
    check =
      (uint32_t *) sc_array_index (checkarray,
                                   (kz - first_quadrant) * (P4EST_DIM + 1));
    check[0] = htonl ((uint32_t) q->x);
    check[1] = htonl ((uint32_t) q->y);
#ifdef P4_TO_P8
    check[2] = htonl ((uint32_t) q->z);
#endif
    check[P4EST_DIM] = htonl ((uint32_t) q->level);
  }
  crc = sc_array_checksum (checkarray);

  if (own_check) {
    sc_array_destroy (checkarray);
  }

  return crc;
}

int
p4est_tree_is_sorted (p4est_tree_t * tree)
{
  size_t              iz;
  p4est_quadrant_t   *q1, *q2;
  sc_array_t         *tquadrants = &tree->quadrants;

  if (tquadrants->elem_count <= 1) {
    return 1;
  }

  q1 = p4est_quadrant_array_index (tquadrants, 0);
  for (iz = 1; iz < tquadrants->elem_count; ++iz) {
    q2 = p4est_quadrant_array_index (tquadrants, iz);
    if (p4est_quadrant_compare (q1, q2) >= 0) {
      return 0;
    }
    q1 = q2;
  }

  return 1;
}

int
p4est_tree_is_linear (p4est_tree_t * tree)
{
  size_t              iz;
  p4est_quadrant_t   *q1, *q2;
  sc_array_t         *tquadrants = &tree->quadrants;

  if (tquadrants->elem_count <= 1) {
    return 1;
  }

  q1 = p4est_quadrant_array_index (tquadrants, 0);
  for (iz = 1; iz < tquadrants->elem_count; ++iz) {
    q2 = p4est_quadrant_array_index (tquadrants, iz);
    if (p4est_quadrant_compare (q1, q2) >= 0) {
      return 0;
    }
    if (p4est_quadrant_is_ancestor (q1, q2)) {
      return 0;
    }
    q1 = q2;
  }

  return 1;
}

int
p4est_tree_is_almost_sorted (p4est_tree_t * tree, int check_linearity)
{
  size_t              iz;
  int                 face_contact1;
  int                 face_contact2;
  int                 out_axis[P4EST_DIM];
  p4est_quadrant_t   *q1, *q2;
  sc_array_t         *tquadrants = &tree->quadrants;

  if (tquadrants->elem_count <= 1) {
    return 1;
  }

  q1 = p4est_quadrant_array_index (tquadrants, 0);
  face_contact1 = 0;
  face_contact1 |= ((q1->x < 0) ? 0x01 : 0);
  face_contact1 |= ((q1->x >= P4EST_ROOT_LEN) ? 0x02 : 0);
  face_contact1 |= ((q1->y < 0) ? 0x04 : 0);
  face_contact1 |= ((q1->y >= P4EST_ROOT_LEN) ? 0x08 : 0);
#ifdef P4_TO_P8
  face_contact1 |= ((q1->z < 0) ? 0x10 : 0);
  face_contact1 |= ((q1->z >= P4EST_ROOT_LEN) ? 0x20 : 0);
#endif
  for (iz = 1; iz < tquadrants->elem_count; ++iz) {
    q2 = p4est_quadrant_array_index (tquadrants, iz);
    face_contact2 = 0;
    face_contact2 |= ((q2->x < 0) ? 0x01 : 0);
    face_contact2 |= ((q2->x >= P4EST_ROOT_LEN) ? 0x02 : 0);
    face_contact2 |= ((q2->y < 0) ? 0x04 : 0);
    face_contact2 |= ((q2->y >= P4EST_ROOT_LEN) ? 0x08 : 0);
#ifdef P4_TO_P8
    face_contact2 |= ((q2->z < 0) ? 0x10 : 0);
    face_contact2 |= ((q2->z >= P4EST_ROOT_LEN) ? 0x20 : 0);
#endif
    out_axis[0] = face_contact2 & 0x03;
    out_axis[1] = face_contact2 & 0x0c;
#ifdef P4_TO_P8
    out_axis[2] = face_contact2 & 0x30;
#endif
    if (((out_axis[0] && out_axis[1])
#ifdef P4_TO_P8
         || (out_axis[0] && out_axis[2])
         || (out_axis[1] && out_axis[2])
#endif
        ) && face_contact1 == face_contact2) {
      /* both quadrants are outside the same edge/corner and can overlap */
    }
    else {
      if (p4est_quadrant_compare (q1, q2) >= 0) {
        return 0;
      }
      if (check_linearity && p4est_quadrant_is_ancestor (q1, q2)) {
        return 0;
      }
    }
    q1 = q2;
    face_contact1 = face_contact2;
  }

  return 1;
}

int
p4est_tree_is_complete (p4est_tree_t * tree)
{
  size_t              iz;
  p4est_quadrant_t   *q1, *q2;
  sc_array_t         *tquadrants = &tree->quadrants;

  if (tquadrants->elem_count <= 1) {
    return 1;
  }

  q1 = p4est_quadrant_array_index (tquadrants, 0);
  for (iz = 1; iz < tquadrants->elem_count; ++iz) {
    q2 = p4est_quadrant_array_index (tquadrants, iz);
    if (!p4est_quadrant_is_next (q1, q2)) {
      return 0;
    }
    q1 = q2;
  }

  return 1;
}

void
p4est_tree_print (int log_priority, p4est_tree_t * tree)
{
  size_t              jz;
  int                 l, childid, comp;
  char                buffer[BUFSIZ];
  p4est_quadrant_t   *q1, *q2;
  sc_array_t         *tquadrants = &tree->quadrants;

  q1 = NULL;
  for (jz = 0; jz < tquadrants->elem_count; ++jz) {
    q2 = p4est_quadrant_array_index (tquadrants, jz);
    childid = p4est_quadrant_child_id (q2);
#ifdef P4_TO_P8
    l = snprintf (buffer, BUFSIZ, "0x%llx 0x%llx 0x%llx %d",
                  (unsigned long long) q2->x, (unsigned long long) q2->y,
                  (unsigned long long) q2->z, (int) q2->level);
#else
    l = snprintf (buffer, BUFSIZ, "0x%llx 0x%llx %d",
                  (unsigned long long) q2->x, (unsigned long long) q2->y,
                  (int) q2->level);
#endif
    if (jz > 0) {
      comp = p4est_quadrant_compare (q1, q2);
      if (comp > 0) {
        l += snprintf (buffer + l, BUFSIZ - l, " R");
      }
      else if (comp == 0) {
        l += snprintf (buffer + l, BUFSIZ - l, " I");
      }
      else {
        if (p4est_quadrant_is_sibling (q1, q2)) {
          l += snprintf (buffer + l, BUFSIZ - l, " S%d", childid);
        }
        else if (p4est_quadrant_is_parent (q1, q2)) {
          l += snprintf (buffer + l, BUFSIZ - l, " C%d", childid);
        }
        else if (p4est_quadrant_is_ancestor (q1, q2)) {
          l += snprintf (buffer + l, BUFSIZ - l, " D");
        }
        else if (p4est_quadrant_is_next (q1, q2)) {
          l += snprintf (buffer + l, BUFSIZ - l, " N%d", childid);
        }
        else {
          l += snprintf (buffer + l, BUFSIZ - l, " q%d", childid);
        }
      }
    }
    else {
      l += snprintf (buffer + l, BUFSIZ - l, " F%d", childid);
    }
    l += snprintf (buffer + l, BUFSIZ - l, "\n");
    P4EST_LOG (log_priority, buffer);
    q1 = q2;
  }
}

int
p4est_is_equal (p4est_t * p4est1, p4est_t * p4est2, int compare_data)
{
  int                 i;
  size_t              zz;
  size_t              data_size;
  p4est_topidx_t      jt;
  p4est_tree_t       *tree1, *tree2;
  p4est_quadrant_t   *q1, *q2;
  sc_array_t         *tqs1, *tqs2;

  if (p4est1->mpisize != p4est2->mpisize)
    return 0;
  if (p4est1->mpirank != p4est2->mpirank)
    return 0;
  if (compare_data) {
    if (p4est1->data_size != p4est2->data_size)
      return 0;
    data_size = p4est1->data_size;
    if (data_size == 0) {
      compare_data = 0;
    }
  }
  else {
    data_size = 0;
  }

  if (p4est1->first_local_tree != p4est2->first_local_tree)
    return 0;
  if (p4est1->last_local_tree != p4est2->last_local_tree)
    return 0;
  if (p4est1->local_num_quadrants != p4est2->local_num_quadrants)
    return 0;
  if (p4est1->global_num_quadrants != p4est2->global_num_quadrants)
    return 0;

  if (memcmp (p4est1->global_first_quadrant, p4est2->global_first_quadrant,
              (p4est1->mpisize + 1) * sizeof (p4est_gloidx_t)))
    return 0;
  if (memcmp (p4est1->global_first_position, p4est2->global_first_position,
              (p4est1->mpisize + 1) * sizeof (p4est_quadrant_t)))
    return 0;

  for (jt = p4est1->first_local_tree; jt <= p4est1->last_local_tree; ++jt) {
    tree1 = p4est_tree_array_index (p4est1->trees, jt);
    tqs1 = &tree1->quadrants;
    tree2 = p4est_tree_array_index (p4est2->trees, jt);
    tqs2 = &tree2->quadrants;

    if (!p4est_quadrant_is_equal (&tree1->first_desc, &tree2->first_desc))
      return 0;
    if (!p4est_quadrant_is_equal (&tree1->last_desc, &tree2->last_desc))
      return 0;
    if (tree1->quadrants_offset != tree2->quadrants_offset)
      return 0;

    for (i = 0; i <= P4EST_MAXLEVEL; ++i) {
      if (tree1->quadrants_per_level[i] != tree2->quadrants_per_level[i])
        return 0;
    }
    if (tree1->maxlevel != tree2->maxlevel)
      return 0;

    if (tqs1->elem_count != tqs2->elem_count)
      return 0;
    for (zz = 0; zz < tqs1->elem_count; ++zz) {
      q1 = p4est_quadrant_array_index (tqs1, zz);
      q2 = p4est_quadrant_array_index (tqs2, zz);
      if (!p4est_quadrant_is_equal (q1, q2))
        return 0;
      if (compare_data
          && memcmp (q1->p.user_data, q2->p.user_data, data_size))
        return 0;
    }
  }

  return 1;
}

int
p4est_is_valid (p4est_t * p4est)
{
  const int           num_procs = p4est->mpisize;
  const int           rank = p4est->mpirank;
  const p4est_topidx_t first_tree = p4est->first_local_tree;
  const p4est_topidx_t last_tree = p4est->last_local_tree;
  int                 i, maxlevel;
  int                 failed;
  p4est_topidx_t      jt, next_tree;
  p4est_locidx_t      nquadrants, lquadrants, perlevel;
  p4est_qcoord_t      mh = P4EST_QUADRANT_LEN (P4EST_QMAXLEVEL);
  p4est_quadrant_t   *q;
  p4est_quadrant_t    mylow, nextlow, s;
  p4est_tree_t       *tree;

  failed = 0;
  P4EST_QUADRANT_INIT (&mylow);
  P4EST_QUADRANT_INIT (&nextlow);
  P4EST_QUADRANT_INIT (&s);

#ifdef P4EST_DEBUG
  /* check last item of global partition */
  P4EST_ASSERT (p4est->global_first_position[num_procs].p.which_tree ==
                p4est->connectivity->num_trees &&
                p4est->global_first_position[num_procs].x == 0 &&
                p4est->global_first_position[num_procs].y == 0);
#ifdef P4_TO_P8
  P4EST_ASSERT (p4est->global_first_position[num_procs].z == 0);
#endif
  P4EST_ASSERT (p4est->connectivity->num_trees ==
                (p4est_topidx_t) p4est->trees->elem_count);
  for (i = 0; i <= num_procs; ++i) {
    P4EST_ASSERT (p4est->global_first_position[i].level == P4EST_QMAXLEVEL);
  }
#endif /* P4EST_DEBUG */

  /* check first tree in global partition */
  if (first_tree < 0) {
    if (!(first_tree == -1 && last_tree == -2)) {
      P4EST_NOTICE ("p4est invalid empty tree range A");
      failed = 1;
      goto failtest;
    }
  }
  else {
    if (p4est->global_first_position[rank].p.which_tree != first_tree) {
      P4EST_NOTICE ("p4est invalid first tree\n");
      failed = 1;
      goto failtest;
    }
    mylow.x = p4est->global_first_position[rank].x;
    mylow.y = p4est->global_first_position[rank].y;
#ifdef P4_TO_P8
    mylow.z = p4est->global_first_position[rank].z;
#endif
    mylow.level = P4EST_QMAXLEVEL;
    tree = p4est_tree_array_index (p4est->trees, first_tree);
    if (tree->quadrants.elem_count > 0) {
      q = p4est_quadrant_array_index (&tree->quadrants, 0);
      if (q->x != mylow.x || q->y != mylow.y ||
#ifdef P4_TO_P8
          q->z != mylow.z ||
#endif
          0) {
        P4EST_NOTICE ("p4est invalid low quadrant\n");
        failed = 1;
        goto failtest;
      }
    }
  }

  /* check last tree in global partition */
  if (last_tree < 0) {
    if (!(first_tree == -1 && last_tree == -2)) {
      P4EST_NOTICE ("p4est invalid empty tree range B");
      failed = 1;
      goto failtest;
    }
  }
  else {
    next_tree = p4est->global_first_position[rank + 1].p.which_tree;
    if (next_tree != last_tree && next_tree != last_tree + 1) {
      P4EST_NOTICE ("p4est invalid last tree\n");
      failed = 1;
      goto failtest;
    }
    nextlow.x = p4est->global_first_position[rank + 1].x;
    nextlow.y = p4est->global_first_position[rank + 1].y;
#ifdef P4_TO_P8
    nextlow.z = p4est->global_first_position[rank + 1].z;
#endif
    nextlow.level = P4EST_QMAXLEVEL;
    if (next_tree == last_tree + 1) {
      if (nextlow.x != 0 || nextlow.y != 0
#ifdef P4_TO_P8
          || nextlow.z != 0
#endif
        ) {
        P4EST_NOTICE ("p4est invalid next coordinates\n");
        failed = 1;
        goto failtest;
      }
    }
    tree = p4est_tree_array_index (p4est->trees, last_tree);
    if (tree->quadrants.elem_count > 0) {
      q =
        p4est_quadrant_array_index (&tree->quadrants,
                                    tree->quadrants.elem_count - 1);
      if (next_tree == last_tree) {
        if (!p4est_quadrant_is_next (q, &nextlow)) {
          P4EST_NOTICE ("p4est invalid next quadrant\n");
          failed = 1;
          goto failtest;
        }
      }
      else {
        p4est_quadrant_last_descendant (q, &s, P4EST_QMAXLEVEL);
        if (s.x + mh != P4EST_ROOT_LEN || s.y + mh != P4EST_ROOT_LEN ||
#ifdef P4_TO_P8
            s.z + mh != P4EST_ROOT_LEN ||
#endif
            0) {
          P4EST_NOTICE ("p4est invalid last quadrant\n");
          failed = 1;
          goto failtest;
        }
      }
    }
  }

  /* check individual trees */
  lquadrants = 0;
  for (jt = 0; jt < (p4est_topidx_t) p4est->trees->elem_count; ++jt) {
    tree = p4est_tree_array_index (p4est->trees, jt);
    if (tree->quadrants_offset != lquadrants) {
      P4EST_NOTICE ("p4est invalid quadrants offset\n");
      failed = 1;
      goto failtest;
    }
    if (!p4est_tree_is_complete (tree)) {
      P4EST_NOTICE ("p4est invalid not complete\n");
      failed = 1;
      goto failtest;
    }
    if (tree->quadrants.elem_count > 0) {
      if (jt < p4est->first_local_tree || jt > p4est->last_local_tree) {
        P4EST_NOTICE ("p4est invalid outside count\n");
        failed = 1;
        goto failtest;
      }
      q = p4est_quadrant_array_index (&tree->quadrants, 0);
      p4est_quadrant_first_descendant (q, &s, P4EST_QMAXLEVEL);
      if (!p4est_quadrant_is_equal (&s, &tree->first_desc)) {
        P4EST_NOTICE ("p4est invalid first tree descendant\n");
        failed = 1;
        goto failtest;
      }
      q =
        p4est_quadrant_array_index (&tree->quadrants,
                                    tree->quadrants.elem_count - 1);
      p4est_quadrant_last_descendant (q, &s, P4EST_QMAXLEVEL);
      if (!p4est_quadrant_is_equal (&s, &tree->last_desc)) {
        P4EST_NOTICE ("p4est invalid last tree descendant\n");
        failed = 1;
        goto failtest;
      }
    }
    else {
      P4EST_QUADRANT_INIT (&s);
      if (s.level != tree->first_desc.level ||
          s.level != tree->last_desc.level) {
        P4EST_NOTICE ("p4est invalid empty descendant\n");
        failed = 1;
        goto failtest;
      }
    }

    maxlevel = 0;
    nquadrants = 0;
    for (i = 0; i <= P4EST_QMAXLEVEL; ++i) {
      perlevel = tree->quadrants_per_level[i];

      P4EST_ASSERT (perlevel >= 0);
      nquadrants += perlevel;   /* same type */
      if (perlevel > 0) {
        maxlevel = i;
      }
    }
    for (; i <= P4EST_MAXLEVEL; ++i) {
      P4EST_ASSERT (tree->quadrants_per_level[i] == -1);
    }
    lquadrants += nquadrants;   /* same type */

    if (maxlevel != (int) tree->maxlevel) {
      P4EST_NOTICE ("p4est invalid wrong maxlevel\n");
      failed = 1;
      goto failtest;
    }
    if (nquadrants != (p4est_locidx_t) tree->quadrants.elem_count) {
      P4EST_NOTICE ("p4est invalid tree quadrant count\n");
      failed = 1;
      goto failtest;
    }
  }

  if (lquadrants != p4est->local_num_quadrants) {
    P4EST_NOTICE ("p4est invalid local quadrant count\n");
    failed = 1;
    goto failtest;
  }

  if (p4est->global_first_quadrant[0] != 0 ||
      p4est->global_first_quadrant[num_procs] !=
      p4est->global_num_quadrants) {
    P4EST_NOTICE ("p4est invalid global quadrant index\n");
    failed = 1;
    goto failtest;
  }

failtest:
  return !p4est_comm_sync_flag (p4est, failed, MPI_BOR);
}

/* here come the heavyweight algorithms */
#ifndef P4_TO_P8
/* which face of the center quad touches this insul */
const static int    insul_to_f[9] = { -1, 2, -1, 0, -1, 1, -1, 3, -1 };

/* which corner of the center quad touches this insul */
const static int    insul_to_c[9] = { 0, -1, 1, -1, -1, -1, 2, -1, 3 };
#else
/* which face of the center quad touches this insul */
/* *INDENT-OFF* */
const static int insul_to_f[27] =
{-1, -1, -1, -1, 4, -1, -1, -1, -1,
 -1, 2, -1, 0, -1, 1, -1, 3, -1,
 -1, -1, -1, -1, 5, -1, -1, -1, -1};
/* which corner of the center quad touches this insul */
const static int insul_to_c[27] =
{0, -1, 1, -1, -1, -1, 2, -1, 3,
 -1, -1, -1, -1, -1, -1, -1, -1, -1,
 4, -1, 5, -1, -1, -1, 6, -1, 7};
/* which edge of the center quad touches this insul */
const static int insul_to_e[27] =
{-1, 0, -1, 4, -1, 5, -1, 1, -1,
  8, -1, 9, -1, -1, -1, 10, -1, 11,
  -1, 2, -1, 6, -1, 7, -1, 3, -1};
/* *INDENT-ON* */
#endif

static void
p4est_tree_compute_overlap_internal (p4est_t * p4est, sc_array_t * in,
                                     sc_array_t * out, int new,
                                     p4est_connect_type_t balance,
                                     sc_array_t * borders)
{
  int                 k, l, m, which;
  int                 face, corner, level;
  int                 f = -1, c = -1;
  int                 ftransform[P4EST_FTRANSFORM];
  int                 face_axis[3];     /* 3 not P4EST_DIM */
  int                 contact_face_only, contact_edge_only;
  int                 inter_tree, outface[P4EST_FACES];
  size_t              iz, jz, ctree;
  size_t              treecount, incount, seedcount;
  size_t              guess, split;
  ssize_t             first_index, last_index, js;
  p4est_topidx_t      qtree, ntree, first_tree, ftree;
  p4est_qcoord_t      qh;
  p4est_quadrant_t    fd, ld, tempq, ins[P4EST_INSUL];
  p4est_quadrant_t   *treefd, *treeld;
  p4est_quadrant_t   *tq, *s, *u;
  p4est_quadrant_t   *inq, *outq;
  p4est_tree_t       *tree;
  p4est_connectivity_t *conn = p4est->connectivity;
#ifdef P4_TO_P8
  int                 edge;
  int                 e = -1;
  size_t              etree;
  p8est_edge_info_t   ei;
  p8est_edge_transform_t *et;
  sc_array_t         *eta;
  p4est_quadrant_t    tempq1, tempq2;
#endif
  p4est_corner_info_t ci;
  p4est_corner_transform_t *ct;
  sc_array_t         *cta;
  sc_array_t         *tquadrants;
  sc_array_t         *seeds;

  P4EST_QUADRANT_INIT (&fd);
  P4EST_QUADRANT_INIT (&ld);
  P4EST_QUADRANT_INIT (&tempq);
#ifdef P4_TO_P8
  P4EST_QUADRANT_INIT (&tempq1);
  P4EST_QUADRANT_INIT (&tempq2);
#endif
  for (which = 0; which < P4EST_INSUL; ++which) {
    P4EST_QUADRANT_INIT (&ins[which]);
  }
#ifdef P4_TO_P8
  eta = &ei.edge_transforms;
  sc_array_init (eta, sizeof (p8est_edge_transform_t));
#endif
  cta = &ci.corner_transforms;
  sc_array_init (cta, sizeof (p4est_corner_transform_t));

  /* assign incoming quadrant count */
  incount = in->elem_count;

  /* initialize the tracking of trees */
  qtree = -1;
  tree = NULL;
  treefd = treeld = NULL;
  tquadrants = NULL;
  treecount = -1;

  if (new) {
    seeds = sc_array_new (sizeof (p4est_quadrant_t));
  }
  first_tree = p4est->first_local_tree;

  /* loop over input list of quadrants */
  for (iz = 0; iz < incount; ++iz) {
    inq = p4est_quadrant_array_index (in, iz);

    if (new) {
      P4EST_ASSERT (inq->p.piggy2.from_tree >= 0 &&
                    inq->p.piggy2.from_tree < p4est->connectivity->num_trees);
      ftree = inq->p.piggy2.from_tree;
    }

    /* potentially grab new tree */
    if (inq->p.piggy2.which_tree != qtree) {
      P4EST_ASSERT (qtree < inq->p.piggy2.which_tree);
      qtree = inq->p.piggy2.which_tree;

      tree = p4est_tree_array_index (p4est->trees, qtree);
      treefd = &tree->first_desc;
      treeld = &tree->last_desc;
      if (borders == NULL) {
        tquadrants = &tree->quadrants;
        treecount = tquadrants->elem_count;
        P4EST_ASSERT (treecount > 0);
      }
      else {
        tquadrants = (sc_array_t *) sc_array_index_int (borders, (int)
                                                        (qtree - first_tree));
        treecount = tquadrants->elem_count;
        P4EST_ASSERT (treecount > 0);
      }
    }

    inter_tree = 0;
    ntree = -1;
    face = corner = -1;
#ifdef P4_TO_P8
    edge = -1;
    ei.iedge = -1;
    et = NULL;
#endif
    ci.icorner = -1;
    ct = NULL;
    contact_face_only = contact_edge_only = 0;
    if (!p4est_quadrant_is_inside_root (inq)) {
      /* this quadrant comes from a different tree */
      P4EST_ASSERT (p4est_quadrant_is_extended (inq));
      inter_tree = 1;
      outface[0] = (inq->x < 0);
      outface[1] = (inq->x >= P4EST_ROOT_LEN);
      face_axis[0] = outface[0] || outface[1];
      outface[2] = (inq->y < 0);
      outface[3] = (inq->y >= P4EST_ROOT_LEN);
      face_axis[1] = outface[2] || outface[3];
#ifndef P4_TO_P8
      face_axis[2] = 0;
#else
      outface[4] = (inq->z < 0);
      outface[5] = (inq->z >= P4EST_ROOT_LEN);
      face_axis[2] = outface[4] || outface[5];
#endif
      if (!face_axis[1] && !face_axis[2]) {
        contact_face_only = 1;
        face = 0 + outface[1];
      }
      else if (!face_axis[0] && !face_axis[2]) {
        contact_face_only = 1;
        face = 2 + outface[3];
      }
#ifdef P4_TO_P8
      else if (!face_axis[0] && !face_axis[1]) {
        contact_face_only = 1;
        face = 4 + outface[5];
      }
      else if (!face_axis[0]) {
        contact_edge_only = 1;
        edge = 0 + 2 * outface[5] + outface[3];
      }
      else if (!face_axis[1]) {
        contact_edge_only = 1;
        edge = 4 + 2 * outface[5] + outface[1];
      }
      else if (!face_axis[2]) {
        contact_edge_only = 1;
        edge = 8 + 2 * outface[3] + outface[1];
      }
#endif
      if (contact_face_only) {
        P4EST_ASSERT (!contact_edge_only && face >= 0 && face < P4EST_FACES);
        P4EST_ASSERT (outface[face]);
        ntree = p4est_find_face_transform (conn, qtree, face, ftransform);
        P4EST_ASSERT (ntree >= 0);
      }
#ifdef P4_TO_P8
      else if (contact_edge_only) {
        P4EST_ASSERT (!contact_face_only && edge >= 0 && edge < P8EST_EDGES);
        p8est_find_edge_transform (conn, qtree, edge, &ei);
        P4EST_ASSERT (ei.edge_transforms.elem_count > 0);
      }
#endif
      else {
        /* outside across a corner */
        P4EST_ASSERT (face_axis[0] && face_axis[1]);
        corner = outface[1] + 2 * outface[3];
#ifdef P4_TO_P8
        P4EST_ASSERT (face_axis[2]);
        corner += 4 * outface[5];
#endif
        P4EST_ASSERT (p4est_quadrant_touches_corner (inq, corner, 0));
        p4est_find_corner_transform (conn, qtree, corner, &ci);
        P4EST_ASSERT (ci.corner_transforms.elem_count > 0);
      }
    }
    qh = P4EST_QUADRANT_LEN (inq->level);

    /* loop over the insulation layer of inq */
#ifdef P4_TO_P8
    for (m = 0; m < 3; ++m) {
#if 0
    }
#endif
#else
    m = 0;
#endif
    for (k = 0; k < 3; ++k) {
      for (l = 0; l < 3; ++l) {
        which = m * 9 + k * 3 + l;      /* 2D: 0..8, 3D: 0..26 */

        /* exclude myself from the queries */
        if (which == P4EST_INSUL / 2) {
          continue;
        }
        s = &ins[which];
        *s = *inq;
        s->x += (l - 1) * qh;
        s->y += (k - 1) * qh;
#ifdef P4_TO_P8
        s->z += (m - 1) * qh;
#endif
        if ((s->x < 0 || s->x >= P4EST_ROOT_LEN) ||
            (s->y < 0 || s->y >= P4EST_ROOT_LEN) ||
#ifdef P4_TO_P8
            (s->z < 0 || s->z >= P4EST_ROOT_LEN) ||
#endif
            0) {
          /* this quadrant is outside this tree, no overlap */
          continue;
        }
        p4est_quadrant_first_descendant (s, &fd, P4EST_QMAXLEVEL);
        p4est_quadrant_last_descendant (s, &ld, P4EST_QMAXLEVEL);

        /* skip this insulation quadrant if there is no overlap */
        if (p4est_quadrant_compare (&ld, treefd) < 0 ||
            p4est_quadrant_compare (treeld, &fd) < 0) {
          continue;
        }

        /* Find first quadrant in tree that fits between fd and ld.
           We are only interested in tree quadrants that are NOT
           larger than s, since this function is used for balance. */
        guess = treecount / 2;
        if (p4est_quadrant_compare (&fd, treefd) <= 0) {
          /* the first tree quadrant overlaps an insulation quadrant */
          first_index = 0;
        }
        else {
          /* Do a binary search for the lowest tree quadrant >= s.
             Does not accept an ancestor of s, which is on purpose. */
          first_index = p4est_find_lower_bound (tquadrants, s, guess);
          if (first_index < 0) {
            continue;
          }
          guess = (size_t) first_index;
        }

        /* find last quadrant in tree that fits between fd and ld */
        if (p4est_quadrant_compare (treeld, &ld) <= 0) {
          /* the last tree quadrant overlaps an insulation quadrant */
          last_index = (ssize_t) treecount - 1;
        }
        else {
          /* do a binary search for the highest tree quadrant <= ld */
          last_index = p4est_find_higher_bound (tquadrants, &ld, guess);
          if (last_index < 0) {
            SC_ABORT_NOT_REACHED ();
          }
        }

        /* skip if no overlap of sufficient level difference is found */
        if (first_index > last_index) {
          continue;
        }

        if (new) {
          /* figure out the relationship of s to inq */
          f = insul_to_f[which];
#ifdef P4_TO_P8
          e = insul_to_e[which];
#endif
          c = insul_to_c[which];

          level = inq->level + 1;

          /* copy relevant quadrants into out */
          for (js = first_index; js <= last_index; ++js) {
            tq = p4est_quadrant_array_index (tquadrants, (size_t) js);
            if (tq->level <= level) {
              continue;
            }
            if (f >= 0) {
              split = !p4est_balance_face_test (tq, inq, f, balance, seeds);
            }
#ifdef P4_TO_P8
            else if (e >= 0) {
              split = !p8est_balance_edge_test (tq, inq, e, balance, seeds);
            }
#endif
            else {
              P4EST_ASSERT (c >= 0);
              split = !p4est_balance_corner_test (tq, inq, c, balance, seeds);
            }
            if (split) {
              seedcount = seeds->elem_count;
              for (jz = 0; jz < seedcount; jz++) {
                u = p4est_quadrant_array_index (seeds, jz);
                P4EST_ASSERT (p4est_quadrant_is_ancestor (inq, u));
                if (inter_tree) {
                  if (contact_face_only) {
                    P4EST_ASSERT (!contact_edge_only);
                    P4EST_ASSERT (ntree == ftree);
                    p4est_quadrant_transform_face (u, &tempq, ftransform);
                    outq = p4est_quadrant_array_push (out);
                    p4est_quadrant_sibling (&tempq, outq, 0);
                    outq->p.piggy2.which_tree = ntree;
                  }
#ifdef P4_TO_P8
                  else if (contact_edge_only) {
                    for (etree = 0; etree < eta->elem_count; ++etree) {
                      et = p8est_edge_array_index (eta, etree);
                      if (et->ntree == ftree) {
                        p8est_quadrant_transform_edge (u, &tempq, &ei, et, 1);
                        outq = p4est_quadrant_array_push (out);
                        p4est_quadrant_sibling (&tempq, outq, 0);
                        outq->p.piggy2.which_tree = et->ntree;
                      }
                    }
                    et = NULL;
                  }
#endif
                  else {
                    P4EST_ASSERT (corner >= 0);
                    for (ctree = 0; ctree < cta->elem_count; ++ctree) {
                      ct = p4est_corner_array_index (cta, ctree);
                      if (ct->ntree == ftree) {
                        p4est_quadrant_transform_corner (u, (int) ct->ncorner,
                                                         1);
                        outq = p4est_quadrant_array_push (out);
                        p4est_quadrant_sibling (u, outq, 0);
                        outq->p.piggy2.which_tree = ct->ntree;
                      }
                    }
                    ct = NULL;
                  }
                }
                else {
                  outq = p4est_quadrant_array_push (out);
                  p4est_quadrant_sibling (u, outq, 0);
                  outq->p.piggy2.which_tree = qtree;
                }

                if (c >= 0) {
                  level = SC_MAX (level, u->level);
                }
              }
            }
          }
        }
        else {
          /* copy relevant quadrants into out */
          if (inter_tree && corner >= 0) {
            /* across a corner, find smallest quadrant to be sent */
            level = 0;
            for (js = first_index; js <= last_index; ++js) {
              tq = p4est_quadrant_array_index (tquadrants, (size_t) js);
              if ((int) tq->level <= SC_MAX (level, (int) inq->level + 1)) {
                continue;
              }
              p4est_quadrant_shift_corner (tq, &tempq, corner);
              P4EST_ASSERT (p4est_quadrant_is_ancestor (s, &tempq));
              level = SC_MAX (level, (int) tempq.level);
            }
            if (level > 0) {
              /* send this small corner to all neighbor corner trees */
              for (ctree = 0; ctree < cta->elem_count; ++ctree) {
                outq = p4est_quadrant_array_push (out);
                outq->level = (int8_t) level;
                ct = p4est_corner_array_index (cta, ctree);
                p4est_quadrant_transform_corner (outq, (int) ct->ncorner, 0);
                outq->p.piggy2.which_tree = ct->ntree;
              }
              ct = NULL;
            }
          }
          else {
            /* across face/edge or intra-tree, find small enough quadrants */
            P4EST_ASSERT (corner == -1);
            for (js = first_index; js <= last_index; ++js) {
              tq = p4est_quadrant_array_index (tquadrants, (size_t) js);
              if (tq->level > inq->level + 1) {
                P4EST_ASSERT (p4est_quadrant_is_ancestor (s, tq));
                if (inter_tree) {
                  if (contact_face_only) {
                    P4EST_ASSERT (!contact_edge_only);
                    outq = p4est_quadrant_array_push (out);
                    p4est_quadrant_transform_face (tq, outq, ftransform);
                    outq->p.piggy2.which_tree = ntree;
                  }
#ifdef P4_TO_P8
                  else {
                    P4EST_ASSERT (contact_edge_only);
                    p8est_quadrant_shift_edge (tq, &tempq, &tempq1, &tempq2,
                                               edge);
                    if (tempq.level > inq->level + 1) {
                      P4EST_ASSERT (p4est_quadrant_is_ancestor (s, &tempq));
                      for (etree = 0; etree < eta->elem_count; ++etree) {
                        outq = p4est_quadrant_array_push (out);
                        et = p8est_edge_array_index (eta, etree);
                        p8est_quadrant_transform_edge (&tempq, outq, &ei, et,
                                                       0);
                        outq->p.piggy2.which_tree = et->ntree;
                      }
                      if (p4est_quadrant_is_ancestor (s, &tempq1)) {
                        for (etree = 0; etree < eta->elem_count; ++etree) {
                          outq = p4est_quadrant_array_push (out);
                          et = p8est_edge_array_index (eta, etree);
                          p8est_quadrant_transform_edge (&tempq1, outq, &ei,
                                                         et, 0);
                          outq->p.piggy2.which_tree = et->ntree;
                        }
                      }
                      if (p4est_quadrant_is_ancestor (s, &tempq2)) {
                        for (etree = 0; etree < eta->elem_count; ++etree) {
                          outq = p4est_quadrant_array_push (out);
                          et = p8est_edge_array_index (eta, etree);
                          p8est_quadrant_transform_edge (&tempq2, outq, &ei,
                                                         et, 0);
                          outq->p.piggy2.which_tree = et->ntree;
                        }
                      }
                    }
                    et = NULL;
                  }
#endif
                }
                else {
                  outq = p4est_quadrant_array_push (out);
                  *outq = *tq;
                  outq->p.piggy2.which_tree = qtree;
                }
              }
            }
          }
        }
      }
    }
#ifdef P4_TO_P8
#if 0
    {
#endif
    }
#endif
  }

#ifdef P4_TO_P8
  sc_array_reset (eta);
#endif
  sc_array_reset (cta);

  if (new) {
    sc_array_destroy (seeds);
  }
}

void
p4est_tree_compute_overlap_new (p4est_t * p4est, sc_array_t * in,
                                sc_array_t * out,
                                p4est_connect_type_t balance,
                                sc_array_t * borders)
{
  p4est_tree_compute_overlap_internal (p4est, in, out, 1, balance, borders);
}

/* warning: p4est_tree_compute_overlap may not work properly for less than
 * full balancing (i.e. face only, face/edge only balancing)
 *
 * TODO: make p4et_tree_compute_overlap take p4est_connect_type_t to guarantee
 * correct behavior for all balance types
 */
void
p4est_tree_compute_overlap (p4est_t * p4est, sc_array_t * in,
                            sc_array_t * out)
{
  p4est_tree_compute_overlap_internal (p4est, in, out, 0, -1, NULL);
}

void
p4est_tree_uniqify_overlap (sc_array_t * skip, sc_array_t * out)
{
  size_t              iz, jz;
  size_t              outcount, dupcount, skipcount;
  size_t              olcount;
  p4est_quadrant_t   *inq, *outq, *tq;

  outcount = out->elem_count;
  if (outcount == 0) {
    return;
  }

  /* sort array and remove duplicates */
  sc_array_sort (out, p4est_quadrant_compare_piggy);
  dupcount = skipcount = olcount = 0;
  iz = 0;                       /* read counter */
  jz = 0;                       /* write counter */
  inq = p4est_quadrant_array_index (out, iz);
  while (iz < outcount) {
    tq =
      (iz < outcount - 1) ? p4est_quadrant_array_index (out, iz + 1) : NULL;
    if (iz < outcount - 1 && p4est_quadrant_is_equal_piggy (inq, tq)) {
      ++dupcount;
      ++iz;
    }
#if 0
    else if (iz < outcount - 1 && inq.p.which_tree == tq.p.which_tree &&
             p4est_quadrant_is_ancestor (inq, tq)) {
      ++olcount;
      ++iz;
    }
#endif
    else if (sc_array_bsearch (skip, inq, p4est_quadrant_compare_piggy) != -1) {
      ++skipcount;
      ++iz;
    }
    else {
      if (iz > jz) {
        outq = p4est_quadrant_array_index (out, jz);
        *outq = *inq;
      }
      ++iz;
      ++jz;
    }
    inq = tq;
  }
  P4EST_ASSERT (iz == outcount);
  P4EST_ASSERT (jz + dupcount + olcount + skipcount == outcount);
  sc_array_resize (out, jz);
}

size_t
p4est_tree_remove_nonowned (p4est_t * p4est, p4est_topidx_t which_tree)
{
  int                 full_tree[2];
  size_t              zz, incount, prev_good, removed;
#ifdef P4EST_DEBUG
  const p4est_topidx_t first_tree = p4est->first_local_tree;
  const p4est_topidx_t last_tree = p4est->last_local_tree;
#endif
  const p4est_quadrant_t *first_pos, *next_pos;
  p4est_quadrant_t   *q1, *q2;
  p4est_quadrant_t    ld;
  p4est_tree_t       *tree;
  sc_array_t         *quadrants;

  P4EST_ASSERT (first_tree <= which_tree && which_tree <= last_tree);
  tree = p4est_tree_array_index (p4est->trees, which_tree);
  P4EST_ASSERT (p4est_tree_is_almost_sorted (tree, 0));

  quadrants = &tree->quadrants;
  incount = quadrants->elem_count;
  if (incount == 0) {
    return 0;
  }

  P4EST_QUADRANT_INIT (&ld);
  p4est_comm_tree_info (p4est, which_tree, full_tree, NULL,
                        &first_pos, &next_pos);

  /* q1 is the last known good quadrant */
  q1 = NULL;
  prev_good = incount;
  removed = 0;
  for (zz = 0; zz < incount; ++zz) {
    q2 = p4est_quadrant_array_index (quadrants, zz);
    P4EST_ASSERT (p4est_quadrant_is_extended (q2));
    if (!p4est_quadrant_is_inside_root (q2) ||
        (!full_tree[0] &&
         (p4est_quadrant_compare (q2, first_pos) < 0 &&
          (q2->x != first_pos->x || q2->y != first_pos->y
#ifdef P4_TO_P8
           || q2->z != first_pos->z
#endif
          ))) ||
        (!full_tree[1] &&
         (p4est_quadrant_last_descendant (q2, &ld, P4EST_QMAXLEVEL),
          p4est_quadrant_compare (next_pos, &ld) <= 0))) {
      /* quadrant is outside of the unit square
         or at least partially outside of the tree bounds */
      --tree->quadrants_per_level[q2->level];
      p4est_quadrant_free_data (p4est, q2);
      ++removed;
#ifdef P4EST_DEBUG
      P4EST_QUADRANT_INIT (q2);
#endif
    }
    else {
      if (prev_good == incount) {
        /* this is the first good quadrant we find */
        prev_good = 0;
      }
      else {
        /* q1 at prev_good was the last known good */
        ++prev_good;
      }
      P4EST_ASSERT (prev_good <= zz);
      q1 = p4est_quadrant_array_index (quadrants, prev_good);
      if (zz > prev_good) {
        *q1 = *q2;
#ifdef P4EST_DEBUG
        P4EST_QUADRANT_INIT (q2);
#endif
      }
    }
  }

  if (prev_good == incount) {
    P4EST_ASSERT (removed == incount);
    incount = 0;
  }
  else {
    P4EST_ASSERT (prev_good + 1 + removed == incount);
    incount = prev_good + 1;
    q1 = p4est_quadrant_array_index (quadrants, 0);
  }
  sc_array_resize (quadrants, incount);

  tree->maxlevel = 0;
  for (zz = 0; zz < incount; ++zz) {
    q1 = p4est_quadrant_array_index (quadrants, zz);
    P4EST_ASSERT (p4est_quadrant_is_valid (q1));
    tree->maxlevel = (int8_t) SC_MAX (tree->maxlevel, q1->level);
  }

  P4EST_ASSERT (p4est_tree_is_sorted (tree));

  return removed;
}

void
p4est_complete_region (p4est_t * p4est,
                       const p4est_quadrant_t * q1,
                       int include_q1,
                       const p4est_quadrant_t * q2,
                       int include_q2,
                       p4est_tree_t * tree,
                       p4est_topidx_t which_tree, p4est_init_t init_fn)
{
#ifdef P4EST_DEBUG
  size_t              quadrant_pool_size, data_pool_size;
#endif

  p4est_tree_t       *R;
  sc_list_t          *W;

  p4est_quadrant_t    a = *q1;
  p4est_quadrant_t    b = *q2;

  p4est_quadrant_t    Afinest;
  p4est_quadrant_t   *c0, *c1, *c2, *c3;
#ifdef P4_TO_P8
  p4est_quadrant_t   *c4, *c5, *c6, *c7;
#endif

  sc_array_t         *quadrants;
  sc_mempool_t       *quadrant_pool = p4est->quadrant_pool;

  p4est_quadrant_t   *w;
  p4est_quadrant_t   *r;

  int                 comp;
  int                 maxlevel = 0;
  p4est_locidx_t     *quadrants_per_level;

  P4EST_QUADRANT_INIT (&Afinest);

  W = sc_list_new (NULL);
  R = tree;

  /* needed for sanity check */
#ifdef P4EST_DEBUG
  quadrant_pool_size = p4est->quadrant_pool->elem_count;
  data_pool_size = 0;
  if (p4est->user_data_pool != NULL) {
    data_pool_size = p4est->user_data_pool->elem_count;
  }
#endif

  quadrants = &R->quadrants;
  quadrants_per_level = R->quadrants_per_level;

  /* Assert that we have an empty tree */
  P4EST_ASSERT (quadrants->elem_count == 0);

  comp = p4est_quadrant_compare (&a, &b);
  /* Assert that a<b */
  P4EST_ASSERT (comp < 0);

  /* R <- R + a */
  if (include_q1) {
    r = p4est_quadrant_array_push (quadrants);
    *r = a;
    p4est_quadrant_init_data (p4est, which_tree, r, init_fn);
    maxlevel = SC_MAX ((int) r->level, maxlevel);
    ++quadrants_per_level[r->level];
  }

  if (comp < 0) {
    /* W <- C(A_{finest}(a,b)) */
    p4est_nearest_common_ancestor (&a, &b, &Afinest);

    c0 = p4est_quadrant_mempool_alloc (quadrant_pool);
    c1 = p4est_quadrant_mempool_alloc (quadrant_pool);
    c2 = p4est_quadrant_mempool_alloc (quadrant_pool);
    c3 = p4est_quadrant_mempool_alloc (quadrant_pool);
#ifdef P4_TO_P8
    c4 = p4est_quadrant_mempool_alloc (quadrant_pool);
    c5 = p4est_quadrant_mempool_alloc (quadrant_pool);
    c6 = p4est_quadrant_mempool_alloc (quadrant_pool);
    c7 = p4est_quadrant_mempool_alloc (quadrant_pool);

    p8est_quadrant_children (&Afinest, c0, c1, c2, c3, c4, c5, c6, c7);
#else
    p4est_quadrant_children (&Afinest, c0, c1, c2, c3);
#endif

    sc_list_append (W, c0);
    sc_list_append (W, c1);
    sc_list_append (W, c2);
    sc_list_append (W, c3);
#ifdef P4_TO_P8
    sc_list_append (W, c4);
    sc_list_append (W, c5);
    sc_list_append (W, c6);
    sc_list_append (W, c7);
#endif

    /* for each w in W */
    while (W->elem_count > 0) {
      w = p4est_quadrant_list_pop (W);

      /* if (a < w < b) and (w not in {A(b)}) */
      if (((p4est_quadrant_compare (&a, w) < 0) &&
           (p4est_quadrant_compare (w, &b) < 0)
          ) && !p4est_quadrant_is_ancestor (w, &b)
        ) {
        /* R <- R + w */
        r = p4est_quadrant_array_push (quadrants);
        *r = *w;
        p4est_quadrant_init_data (p4est, which_tree, r, init_fn);
        maxlevel = SC_MAX ((int) r->level, maxlevel);
        ++quadrants_per_level[r->level];
      }
      /* else if (w in {{A(a)}, {A(b)}}) */
      else if (p4est_quadrant_is_ancestor (w, &a)
               || p4est_quadrant_is_ancestor (w, &b)) {
        /* W <- W + C(w) */
        c0 = p4est_quadrant_mempool_alloc (quadrant_pool);
        c1 = p4est_quadrant_mempool_alloc (quadrant_pool);
        c2 = p4est_quadrant_mempool_alloc (quadrant_pool);
        c3 = p4est_quadrant_mempool_alloc (quadrant_pool);
#ifdef P4_TO_P8
        c4 = p4est_quadrant_mempool_alloc (quadrant_pool);
        c5 = p4est_quadrant_mempool_alloc (quadrant_pool);
        c6 = p4est_quadrant_mempool_alloc (quadrant_pool);
        c7 = p4est_quadrant_mempool_alloc (quadrant_pool);

        p8est_quadrant_children (w, c0, c1, c2, c3, c4, c5, c6, c7);
#else
        p4est_quadrant_children (w, c0, c1, c2, c3);
#endif

#ifdef P4_TO_P8
        sc_list_prepend (W, c7);
        sc_list_prepend (W, c6);
        sc_list_prepend (W, c5);
        sc_list_prepend (W, c4);
#endif
        sc_list_prepend (W, c3);
        sc_list_prepend (W, c2);
        sc_list_prepend (W, c1);
        sc_list_prepend (W, c0);
      }

      /* W <- W - w */
      sc_mempool_free (quadrant_pool, w);
    }                           /* end for */

    /* R <- R + b */
    if (include_q2) {
      r = p4est_quadrant_array_push (quadrants);
      *r = b;
      p4est_quadrant_init_data (p4est, which_tree, r, init_fn);
      maxlevel = SC_MAX ((int) r->level, maxlevel);
      ++quadrants_per_level[r->level];
    }
  }

  R->maxlevel = (int8_t) maxlevel;

  P4EST_ASSERT (W->first == NULL && W->last == NULL);
  sc_list_destroy (W);

  P4EST_ASSERT (p4est_tree_is_complete (R));
  P4EST_ASSERT (quadrant_pool_size == p4est->quadrant_pool->elem_count);
  if (p4est->user_data_pool != NULL) {
    P4EST_ASSERT (data_pool_size + quadrants->elem_count ==
                  p4est->user_data_pool->elem_count);
  }
}

/** Internal function to realize local completion / balancing.
 * \param [in] balance  can be 0: no balance only completion
 *                      and then in 2D:
 *                             1: balance across edges
 *                             2: balance across edges and corners
 *                      and in 3D:
 *                             1: balance across faces
 *                             2: balance across faces and edges
 *                             3: balance across faces, edges and corners
 */
static void
p4est_complete_or_balance (p4est_t * p4est, p4est_topidx_t which_tree,
                           p4est_init_t init_fn, int balance)
{
  int                 lookup, inserted;
  int                 isfamily, isoutroot;
#ifdef P4EST_BALANCE_OPTIMIZE
  int                 isintree;
#endif
  size_t              iz, jz;
  size_t              incount, ocount;
#ifdef P4EST_DEBUG
  size_t              quadrant_pool_size, data_pool_size;
#endif
  size_t              count_outside_root, count_outside_tree;
  size_t              count_already_inlist, count_already_outlist;
  size_t              count_moved1_outside, count_moved2_outside;
  size_t              num_added, num_nonowned, num_linearized;
  int                 qid, sid, pid, sindex;
  int                 bbound, fbound, rbound;
  int                 skey, *key = &skey;
  int                 pkey, *parent_key = &pkey;
  int                 l, inmaxl;
  void              **vlookup;
  ssize_t             srindex;
  p4est_qcoord_t      ph;
  p4est_quadrant_t   *family[P4EST_CHILDREN];
  p4est_quadrant_t   *q;
  p4est_quadrant_t   *qalloc, *qlookup, **qpointer;
  p4est_quadrant_t    ld, pshift;
  p4est_tree_t       *tree;
  sc_array_t         *inlist, *olist;
  sc_mempool_t       *list_alloc, *qpool;
  sc_hash_t          *hash[P4EST_MAXLEVEL + 1];
  sc_array_t          outlist[P4EST_MAXLEVEL + 1];

  P4EST_ASSERT (which_tree >= p4est->first_local_tree);
  P4EST_ASSERT (which_tree <= p4est->last_local_tree);
  tree = p4est_tree_array_index (p4est->trees, which_tree);

  P4EST_ASSERT (0 <= balance && balance <= P4EST_DIM);
  P4EST_ASSERT (p4est_tree_is_almost_sorted (tree, 1));

  P4EST_QUADRANT_INIT (&ld);
  P4EST_QUADRANT_INIT (&pshift);

  /*
   * Algorithm works with these data structures
   * inlist  --  sorted list of input quadrants
   * hash    --  hash table to hold additional quadrants not in inlist
   *             this is filled bottom-up to ensure balance condition
   * outlist --  filled simultaneously with hash, holding pointers
   *             don't rely on addresses of elements, it is resized
   * In the end, the elements of hash are appended to inlist
   * and inlist is sorted and linearized. This can be optimized later.
   */

  /* assign some shortcut variables */
  fbound = p4est_balance_count[P4EST_DIM];
  bbound = p4est_balance_count[balance];
  inlist = &tree->quadrants;
  incount = inlist->elem_count;
  inmaxl = (int) tree->maxlevel;
  qpool = p4est->quadrant_pool;

  /* needed for sanity check */
#ifdef P4EST_DEBUG
  quadrant_pool_size = qpool->elem_count;
  data_pool_size = 0;
  if (p4est->user_data_pool != NULL) {
    data_pool_size = p4est->user_data_pool->elem_count;
  }
#endif

  /* if tree is empty, there is nothing to do */
  if (incount == 0) {
    return;
  }

  /* initialize some counters */
  count_outside_root = count_outside_tree = 0;
  count_already_inlist = count_already_outlist = 0;
  count_moved1_outside = count_moved2_outside = 0;

  /* initialize temporary storage */
  list_alloc = sc_mempool_new (sizeof (sc_link_t));
  for (l = 0; l <= inmaxl; ++l) {
    hash[l] = sc_hash_new (p4est_quadrant_hash_fn, p4est_quadrant_equal_fn,
                           NULL, list_alloc);
    sc_array_init (&outlist[l], sizeof (p4est_quadrant_t *));
  }
  for (; l <= P4EST_MAXLEVEL; ++l) {
    hash[l] = NULL;
    memset (&outlist[l], -1, sizeof (sc_array_t));
  }

  /* walk through the input tree bottom-up */
  ph = 0;
  pid = -1;
  qalloc = p4est_quadrant_mempool_alloc (qpool);
  qalloc->p.user_data = key;
  for (l = inmaxl; l > 0; --l) {
    ocount = outlist[l].elem_count;     /* fix ocount here, it is growing */
    for (iz = 0; iz < incount + ocount; ++iz) {
      isfamily = 0;
      if (iz < incount) {
        q = p4est_quadrant_array_index (inlist, iz);
        if ((int) q->level != l) {
          continue;
        }
        /* this is an optimization to catch adjacent siblings */
        if (iz + P4EST_CHILDREN <= incount) {
          family[0] = q;
          for (jz = 1; jz < P4EST_CHILDREN; ++jz) {
            family[jz] = p4est_quadrant_array_index (inlist, iz + jz);
          }
          if (p4est_quadrant_is_familypv (family)) {
            isfamily = 1;
            iz += P4EST_CHILDREN - 1;   /* skip siblings */
          }
        }
      }
      else {
        qpointer =
          (p4est_quadrant_t **) sc_array_index (&outlist[l], iz - incount);
        q = *qpointer;
        P4EST_ASSERT ((int) q->level == l);
      }
      P4EST_ASSERT (p4est_quadrant_is_extended (q));
      isoutroot = !p4est_quadrant_is_inside_root (q);
#ifdef P4EST_BALANCE_OPTIMIZE
      if (isoutroot) {
        isintree = 0;
      }
      else {
        /* TODO: verify p4est_quadarant_is_inside_tree function */
        isintree = p4est_quadrant_is_inside_tree (tree, q);
        if (!isintree && p4est_quadrant_overlaps_tree (tree, q)) {
          ++count_moved1_outside;
          continue;
        }
      }
#endif
      /* TODO:
         For inter-tree quadrants always do full edge/corner balance
         May not be necessary and lead to too many quadrants */
      rbound = (isoutroot ? fbound : bbound);

      /*
       * check for q and its siblings,
       * then for q's parent and parent's indirect relevant neighbors
       * 2D
       * sid == 0..3    siblings including q
       *        4       parent of q
       *        5..6    indirect face neighbors of parent
       *        7       indirect corner neighbor of parent
       * 3D
       * sid == 0..7    siblings including q
       *        8       parent of q
       *        9..11   indirect face neighbors of parent
       *        12..14  indirect edge neighbors of parent
       *        15      indirect corner neighbor of parent
       *
       * if q is inside the tree, include all of the above.
       * if q is outside the tree, include only its parent and the neighbors.
       */
      qid = p4est_quadrant_child_id (q);        /* 0 <= qid < 4 resp. 8 */
      for (sid = 0; sid < rbound; ++sid) {
        /* stage 1: determine candidate qalloc */
        if (sid < P4EST_CHILDREN) {
          if (qid == sid || isfamily) {
            /* q (or its family) is included in inlist */
            continue;
          }
          if (isoutroot) {
            /* don't add siblings outside of the unit tree */
            continue;
          }
          p4est_quadrant_sibling (q, qalloc, sid);
        }
        else if (sid == P4EST_CHILDREN) {
          /* compute the parent */
          p4est_quadrant_parent (q, qalloc);
          if (balance > 0) {
            pshift = *qalloc;   /* copy parent for all balance cases */
            ph = P4EST_QUADRANT_LEN (pshift.level);     /* its size */
            pid = p4est_quadrant_child_id (&pshift);    /* and position */
            if (pid > 0 && pshift.level > 0)
              p4est_quadrant_sibling (&pshift, &pshift, 0);
          }
        }
        else {
          if (l == 1) {
            /* don't add tree-size quadrants as parent neighbors */
            break;
          }
          P4EST_ASSERT (sid >= p4est_balance_count[0]);
          if (sid < p4est_balance_count[1]) {
            /* face balance */
            sindex = p4est_corner_faces[pid][sid - p4est_balance_count[0]];
            P4EST_ASSERT (0 <= sindex && sindex < P4EST_FACES);
            qalloc->x = pshift.x + p4est_balance_coord[sindex][0] * ph;
            qalloc->y = pshift.y + p4est_balance_coord[sindex][1] * ph;
#ifdef P4_TO_P8
            qalloc->z = pshift.z + p4est_balance_coord[sindex][2] * ph;
#endif
          }
#ifdef P4_TO_P8
          else if (sid < p4est_balance_count[2]) {
            /* edge balance */
            sindex = p8est_corner_edges[pid][sid - p4est_balance_count[1]];
            P4EST_ASSERT (0 <= sindex && sindex < P8EST_EDGES);
            qalloc->x = pshift.x + p4est_balance_coord[pbeo + sindex][0] * ph;
            qalloc->y = pshift.y + p4est_balance_coord[pbeo + sindex][1] * ph;
            qalloc->z = pshift.z + p4est_balance_coord[pbeo + sindex][2] * ph;
          }
#endif
          else {
            P4EST_ASSERT (sid == p4est_balance_count[P4EST_DIM] - 1);
            /* corner balance */
            qalloc->x = pshift.x + p4est_balance_coord[pbco + pid][0] * ph;
            qalloc->y = pshift.y + p4est_balance_coord[pbco + pid][1] * ph;
#ifdef P4_TO_P8
            qalloc->z = pshift.z + p4est_balance_coord[pbco + pid][2] * ph;
#endif
          }
          qalloc->level = pshift.level;

          /* TODO: Verify optimizations which may omit necessary quadrants */
          if (!isoutroot) {
            if (!p4est_quadrant_is_inside_root (qalloc)) {
              ++count_outside_root;
              continue;
            }
          }
          else {
            if (!p4est_quadrant_is_inside_3x3 (qalloc)) {
              ++count_outside_root;
              continue;
            }
#ifdef P4EST_BALANCE_OPTIMIZE
            if (!p4est_quadrant_is_inside_root (qalloc) &&
                (q->x / P4EST_ROOT_LEN != qalloc->x / P4EST_ROOT_LEN ||
                 q->y / P4EST_ROOT_LEN != qalloc->y / P4EST_ROOT_LEN ||
#ifdef P4_TO_P8
                 q->z / P4EST_ROOT_LEN != qalloc->z / P4EST_ROOT_LEN ||
#endif
                 0)) {
              ++count_outside_root;
              continue;
            }
#endif
          }
        }
        P4EST_ASSERT (p4est_quadrant_is_extended (qalloc));
        /*
           P4EST_DEBUGF ("Candidate level %d qxy 0x%x 0x%x at sid %d\n",
           qalloc->level, qalloc->x, qalloc->y, sid);
         */

        /* stage 2: include qalloc */
#if defined P4EST_BALANCE_WRONG && defined P4EST_BALANCE_OPTIMIZE
        if (isintree && p4est_quadrant_is_inside_root (qalloc) &&
            !p4est_quadrant_is_inside_tree (tree, qalloc)) {
          ++count_moved2_outside;
          continue;
        }
#endif
        /* make sure that qalloc is not included more than once */
        lookup = sc_hash_lookup (hash[qalloc->level], qalloc, &vlookup);
        if (lookup) {
          /* qalloc is already included in output list, this catches most */
          ++count_already_outlist;
          /* *INDENT-OFF* HORRIBLE indent bug */
          qlookup = (p4est_quadrant_t *) *vlookup;
          /* *INDENT-ON* */
          if (sid == P4EST_CHILDREN && qlookup->p.user_data == parent_key) {
            break;              /* this parent has been triggered before */
          }
          continue;
        }
        srindex = sc_array_bsearch (inlist, qalloc, p4est_quadrant_compare);
        if (srindex != -1) {
          /* qalloc is included in inlist, this is more expensive to test */
          ++count_already_inlist;
          continue;
        }
        /* insert qalloc into the output list as well */
        if (sid == P4EST_CHILDREN) {
          qalloc->p.user_data = parent_key;
        }
        inserted = sc_hash_insert_unique (hash[qalloc->level], qalloc, NULL);
        P4EST_ASSERT (inserted);
        olist = &outlist[qalloc->level];
        qpointer = (p4est_quadrant_t **) sc_array_push (olist);
        *qpointer = qalloc;
        /* we need a new quadrant now, the old one is stored away */
        qalloc = p4est_quadrant_mempool_alloc (qpool);
        qalloc->p.user_data = key;
      }
    }
  }
  sc_mempool_free (qpool, qalloc);

  /* merge outlist into input list and free temporary storage */
  P4EST_LDEBUGF ("Hash statistics for tree %lld\n", (long long) which_tree);
  num_added = 0;
  for (l = 0; l <= inmaxl; ++l) {
    /* print statistics and free hash tables */
#ifdef P4EST_DEBUG
    sc_hash_print_statistics (p4est_package_id, SC_LP_DEBUG, hash[l]);
#endif
    sc_hash_unlink_destroy (hash[l]);

    /* merge valid quadrants from outlist into inlist */
    ocount = outlist[l].elem_count;
    q = NULL;
    for (iz = 0; iz < ocount; ++iz) {
      /* go through output list */
      qpointer = (p4est_quadrant_t **) sc_array_index (&outlist[l], iz);
      qalloc = *qpointer;
      P4EST_ASSERT ((int) qalloc->level == l);
      P4EST_ASSERT (qalloc->p.user_data == key ||
                    qalloc->p.user_data == parent_key);
      if (p4est_quadrant_is_inside_root (qalloc)) {
        /* copy temporary quadrant into final tree */
        q = p4est_quadrant_array_push (inlist);
        *q = *qalloc;
        ++num_added;
        ++tree->quadrants_per_level[l];

        /* complete quadrant initialization */
        p4est_quadrant_init_data (p4est, which_tree, q, init_fn);
      }
      else {
        P4EST_ASSERT (p4est_quadrant_is_extended (qalloc));
      }
      sc_mempool_free (qpool, qalloc);
    }
    if (q != NULL && l > (int) tree->maxlevel) {
      tree->maxlevel = (int8_t) l;
    }
    sc_array_reset (&outlist[l]);
  }
  sc_mempool_destroy (list_alloc);
  P4EST_ASSERT (incount + num_added == inlist->elem_count);

  /* print more statistics */
  P4EST_VERBOSEF ("Tree %lld Outside root %llu tree %llu\n",
                  (long long) which_tree,
                  (unsigned long long) count_outside_root,
                  (unsigned long long) count_outside_tree);
  P4EST_VERBOSEF
    ("Tree %lld inlist %llu outlist %llu moved %llu %llu insert %llu\n",
     (long long) which_tree, (unsigned long long) count_already_inlist,
     (unsigned long long) count_moved1_outside,
     (unsigned long long) count_moved2_outside,
     (unsigned long long) count_already_outlist,
     (unsigned long long) num_added);

  /* sort and linearize tree */
  sc_array_sort (inlist, p4est_quadrant_compare);
  num_nonowned = p4est_tree_remove_nonowned (p4est, which_tree);
  num_linearized = p4est_linearize_tree (p4est, tree);

  /* run sanity checks */
  P4EST_ASSERT (quadrant_pool_size == qpool->elem_count);
  if (p4est->user_data_pool != NULL) {
    P4EST_ASSERT (data_pool_size + inlist->elem_count ==
                  p4est->user_data_pool->elem_count + incount);
  }
  P4EST_ASSERT (incount + num_added - num_nonowned - num_linearized ==
                tree->quadrants.elem_count);

  P4EST_ASSERT (p4est_tree_is_complete (tree));
}

void
p4est_balance_border (p4est_t * p4est, p4est_connect_type_t btype,
                      p4est_topidx_t which_tree, p4est_init_t init_fn,
                      sc_array_t * borders)
{
  int                 lookup, inserted;
  size_t              iz, jz, kz;
  size_t              incount, ocount;
#ifdef P4EST_DEBUG
  size_t              quadrant_pool_size, data_pool_size;
#endif
  size_t              count_already_inlist, count_already_outlist;
  size_t              count_ancestor_inlist;
  p4est_tree_t       *tree;
  p4est_quadrant_t   *q, *p, *r;
  sc_array_t          qview;
  sc_array_t         *inlist, *flist, *tquadrants;
  sc_array_t          tqview;
  size_t              tqoffset;
  int                 maxlevel, minlevel;
  p4est_topidx_t      first_tree = p4est->first_local_tree;
  size_t              num_added, num_this_added;
  int                 qid, sid, pid;
  int                 bound;
  int                 skey, *key = &skey;
  int                 bkey, *block_key = &bkey;
  int                 l;
  void              **vlookup;
  ssize_t             srindex, tqindex;
  size_t              tqorig, tqold;
  p4est_qcoord_t      ph;
  p4est_quadrant_t   *qalloc, **qpointer;
  p4est_quadrant_t    par, tempq, tempp;
  sc_array_t         *olist;
  sc_mempool_t       *list_alloc, *qpool;
  sc_hash_t          *hash[P4EST_MAXLEVEL + 1];
  sc_array_t          outlist[P4EST_MAXLEVEL + 1];
  /* get this tree's border */
  sc_array_t         *qarray = (sc_array_t *) sc_array_index (borders,
                                                              which_tree -
                                                              first_tree);
  size_t              qcount = qarray->elem_count;

  if (!qcount) {
    /* nothing to be done */
    return;
  }

  /* set up balance machinery */

  if (btype == P4EST_CONNECT_FULL) {
    bound = (1 << P4EST_DIM);
  }
#ifdef P4_TO_P8
  else if (btype == P8EST_CONNECT_EDGE) {
    bound = (1 << P4EST_DIM) - 1;
  }
#endif
  else {
    bound = P4EST_DIM + 1;
  }

  P4EST_ASSERT (which_tree >= p4est->first_local_tree);
  P4EST_ASSERT (which_tree <= p4est->last_local_tree);

  tree = p4est_tree_array_index (p4est->trees, which_tree);
  tquadrants = &(tree->quadrants);
  tqorig = tquadrants->elem_count;
  tqoffset = 0;
  sc_array_init_view (&tqview, tquadrants, tqoffset,
                      tquadrants->elem_count - tqoffset);

  P4EST_QUADRANT_INIT (&par);
  P4EST_QUADRANT_INIT (&tempq);
  P4EST_QUADRANT_INIT (&tempp);

  qpool = p4est->quadrant_pool;

#ifdef P4EST_DEBUG
  quadrant_pool_size = qpool->elem_count;
  data_pool_size = 0;
  if (p4est->user_data_pool != NULL) {
    data_pool_size = p4est->user_data_pool->elem_count;
  }
#endif

  count_already_inlist = count_already_outlist = 0;
  count_ancestor_inlist = 0;
  num_added = 0;

  /* initialize temporary storage */
  list_alloc = sc_mempool_new (sizeof (sc_link_t));

  inlist = sc_array_new (sizeof (p4est_quadrant_t));
  flist = sc_array_new (sizeof (p4est_quadrant_t));

  /* sort the border and remove duplicates */
  sc_array_sort (qarray, p4est_quadrant_compare);
  jz = 1;                       /* number included */
  kz = 0;                       /* number skipped */
  p = p4est_quadrant_array_index (qarray, 0);
  P4EST_ASSERT (p4est_quadrant_is_valid (p));
  for (iz = 1; iz < qcount; iz++) {
    q = p4est_quadrant_array_index (qarray, iz);
    P4EST_ASSERT (p4est_quadrant_is_extended (q));
    if (!p4est_quadrant_is_equal (q, p)) {
      p++;
      jz++;
      if (!kz) {
        *p = *q;
      }
    }
    else {
      kz++;
    }
  }
  P4EST_ASSERT (kz + jz == qcount);
  sc_array_resize (qarray, jz);
  qcount = jz;

  /* step through border */
  for (iz = 0; iz < qcount; iz++) {
    p = p4est_quadrant_array_index (qarray, iz);

    if (p4est_quadrant_compare (p, &(tree->first_desc)) < 0 &&
        !p4est_quadrant_is_ancestor (p, &(tree->first_desc))) {
      continue;
    }
    if (p4est_quadrant_compare (p, &(tree->last_desc)) > 0) {
      continue;
    }

    P4EST_ASSERT (p4est_quadrant_is_valid (p));

    /* get a view of all of the quads that are descended from this quad */
    jz = iz + 1;
    kz = jz;

    minlevel = p->level + 1;
    maxlevel = minlevel;

    if (kz < qcount) {
      q = p4est_quadrant_array_index (qarray, kz);
    }

    while (kz < qcount && p4est_quadrant_is_ancestor (p, q)) {
      kz++;

      maxlevel = SC_MAX (maxlevel, q->level);
      P4EST_ASSERT (p4est_quadrant_child_id (q) == 0);
      q->p.user_data = key;

      if (kz < qcount) {
        q = p4est_quadrant_array_index (qarray, kz);
      }
    }

    incount = kz - jz;
    if (!incount) {
      continue;
    }

    /* find p in tquadrants */
    tqindex = sc_array_bsearch (&tqview, p, p4est_quadrant_compare);

    P4EST_ASSERT (tqindex >= 0);

    /* update the view of tquadrants to be everything past p */
    tqindex += tqoffset;        /* tqindex is the index of p in tquadrants */
    tqoffset = tqindex + 1;
    sc_array_init_view (&tqview, tquadrants, tqoffset,
                        tquadrants->elem_count - tqoffset);

    /* get all of the quadrants that descend from p into inlist */
    sc_array_init_view (&qview, qarray, jz, incount);
    sc_array_resize (inlist, incount);
    memcpy (inlist->array, qview.array, incount * sizeof (p4est_quadrant_t));

    /* balance them within the containing quad */

    /* initialize temporary storage */
    for (l = 0; l <= minlevel; ++l) {
      /* we don't need a hash table for minlevel, because all minlevel
       * quadrants will be created automatically by filling in gaps */
      hash[l] = NULL;
      memset (&outlist[l], -1, sizeof (sc_array_t));
    }
    for (; l <= maxlevel; ++l) {
      hash[l] = sc_hash_new (p4est_quadrant_hash_fn, p4est_quadrant_equal_fn,
                             NULL, list_alloc);
      sc_array_init (&outlist[l], sizeof (p4est_quadrant_t *));
    }
    for (; l <= P4EST_MAXLEVEL; ++l) {
      /* we don't need a hash table for maxlevel because a quad only spawns
       * larger quads */
      hash[l] = NULL;
      memset (&outlist[l], -1, sizeof (sc_array_t));
    }

    /* walk through the input tree bottom-up */
    ph = 0;
    pid = -1;
    qalloc = p4est_quadrant_mempool_alloc (qpool);
    qalloc->p.user_data = key;

    /* we don't need to run for minlevel + 1, because all of the quads that
     * would be created would be outside p */
    for (l = maxlevel; l > minlevel + 1; l--) {
      ocount = outlist[l].elem_count;   /* ocount is not growing */
      for (jz = 0; jz < incount + ocount; ++jz) {
        if (jz < incount) {
          q = p4est_quadrant_array_index (inlist, jz);
          if ((int) q->level != l || q->p.user_data == block_key) {
            continue;
          }
        }
        else {
          qpointer =
            (p4est_quadrant_t **) sc_array_index (&outlist[l], jz - incount);
          q = *qpointer;
          P4EST_ASSERT ((int) q->level == l);
        }
        P4EST_ASSERT (p4est_quadrant_is_ancestor (p, q));
        P4EST_ASSERT (p4est_quadrant_child_id (q) == 0);

        qid = p4est_quadrant_child_id (q);      /* 0 <= qid < 4 resp. 8 */

        p4est_quadrant_parent (q, &par);        /* get the parent */
        ph = P4EST_QUADRANT_LEN (par.level - 1);        /* twice its size */
        pid = p4est_quadrant_child_id (&par);   /* and position */
        p4est_quadrant_sibling (&par, &par, 0); /* now shift to 0 */

        for (sid = 0; sid < bound; sid++) {
          *qalloc = par;
          if (!sid) {
            /* include the smallest ancestor that's needed */
            while (qalloc->level > minlevel + 1 &&
                   p4est_quadrant_ancestor_id (qalloc, qalloc->level - 1)
                   == pid) {
              p4est_quadrant_parent (qalloc, qalloc);
            }
            P4EST_ASSERT (p4est_quadrant_is_ancestor (p, qalloc));
            p4est_quadrant_sibling (qalloc, qalloc, 0);
          }
          else if (sid <= P4EST_DIM) {
            /* include face neighbors */
            switch (sid - 1) {
            case 0:
              qalloc->x = par.x + ((pid & 1) ? ph : -ph);
              break;
            case 1:
              qalloc->y = par.y + ((pid & 2) ? ph : -ph);
              break;
#ifdef P4_TO_P8
            case 2:
              qalloc->z = par.z + ((pid & 4) ? ph : -ph);
              break;
#endif
            default:
              SC_ABORT_NOT_REACHED ();
            }
          }
#ifdef P4_TO_P8
          else if (sid < 7) {
            /* include edge neighbors */
            switch (sid - 4) {
            case 0:
              qalloc->y = par.y + ((pid & 2) ? ph : -ph);
              qalloc->z = par.z + ((pid & 4) ? ph : -ph);
              break;
            case 1:
              qalloc->x = par.x + ((pid & 1) ? ph : -ph);
              qalloc->z = par.z + ((pid & 4) ? ph : -ph);
              break;
            case 2:
              qalloc->x = par.x + ((pid & 1) ? ph : -ph);
              qalloc->y = par.y + ((pid & 2) ? ph : -ph);
              break;
            default:
              SC_ABORT_NOT_REACHED ();
            }
          }
#endif
          else {
            /* include corner neighbor */
            qalloc->x = par.x + ((pid & 1) ? ph : -ph);
            qalloc->y = par.y + ((pid & 2) ? ph : -ph);
#ifdef P4_TO_P8
            qalloc->z = par.z + ((pid & 4) ? ph : -ph);
#endif
          }

          P4EST_ASSERT (p4est_quadrant_is_extended (qalloc));
          P4EST_ASSERT (p4est_quadrant_child_id (qalloc) == 0);

          P4EST_ASSERT (sid || p4est_quadrant_is_ancestor (p, qalloc));
          if (sid && !p4est_quadrant_is_ancestor (p, qalloc)) {
            continue;
          }

          /* make sure that qalloc is not included more than once */
          lookup = sc_hash_lookup (hash[qalloc->level], qalloc, &vlookup);
          if (lookup) {
            /* qalloc is already included in output list,
             * this catches most */
            ++count_already_outlist;
            continue;
          }
          srindex =
            sc_array_bsearch (inlist, qalloc, p4est_quadrant_disjoint);

          if (srindex != -1) {
            r = p4est_quadrant_array_index (inlist, srindex);

            if (p4est_quadrant_is_equal (r, qalloc)) {
              ++count_already_inlist;
              continue;
            }
            if (p4est_quadrant_is_ancestor (r, qalloc)) {
              r->p.user_data = block_key;
            }
            else if (sid) {
              /* unless we know that we need this ancestor, do not include it
               * yet */
              P4EST_ASSERT (p4est_quadrant_is_ancestor (qalloc, r));
              ++count_ancestor_inlist;
              continue;
            }
          }

          inserted =
            sc_hash_insert_unique (hash[qalloc->level], qalloc, NULL);
          P4EST_ASSERT (inserted);
          olist = &outlist[qalloc->level];
          qpointer = (p4est_quadrant_t **) sc_array_push (olist);
          *qpointer = qalloc;
          /* we need a new quadrant now, the old one is stored away */
          qalloc = p4est_quadrant_mempool_alloc (qpool);
          qalloc->p.user_data = key;
        }
      }
    }
    sc_mempool_free (qpool, qalloc);

    for (l = minlevel + 1; l <= maxlevel; ++l) {
      /* print statistics and free hash tables */
#ifdef P4EST_DEBUG
      sc_hash_print_statistics (p4est_package_id, SC_LP_DEBUG, hash[l]);
#endif
      sc_hash_unlink_destroy (hash[l]);

      /* merge valid quadrants from outlist into inlist */
      ocount = outlist[l].elem_count;
      q = NULL;
      for (jz = 0; jz < ocount; ++jz) {
        /* go through output list */
        qpointer = (p4est_quadrant_t **) sc_array_index (&outlist[l], jz);
        qalloc = *qpointer;
        P4EST_ASSERT ((int) qalloc->level == l);
        P4EST_ASSERT (p4est_quadrant_is_ancestor (p, qalloc));
        P4EST_ASSERT (p4est_quadrant_child_id (qalloc) == 0);
        /* copy temporary quadrant into inlist */
        q = p4est_quadrant_array_push (inlist);
        *q = *qalloc;
        sc_mempool_free (qpool, qalloc);
      }
      sc_array_reset (&outlist[l]);
    }
    P4EST_ASSERT (quadrant_pool_size == qpool->elem_count);
    sc_mempool_truncate (list_alloc);

    /* sort inlist */
    sc_array_sort (inlist, p4est_quadrant_compare);

    /* step through inlist: ignore blocked quadrants, add the rest,
     * and fill in the gaps in flist */
    sc_array_resize (flist, 0);

    p4est_quadrant_first_descendant (p, &tempq, minlevel);
    incount = inlist->elem_count;
    pid = 0;
    jz = 0;
    P4EST_ASSERT (incount > 0);
    q = p4est_quadrant_array_index (inlist, jz);
    P4EST_ASSERT (p4est_quadrant_child_id (q) == 0);
    for (;;) {
      /* while tempq comes before q */
      while (q == NULL || (!p4est_quadrant_is_equal (&tempq, q) &&
                           !p4est_quadrant_is_ancestor (&tempq, q))) {
        P4EST_ASSERT (q == NULL || p4est_quadrant_compare (&tempq, q) < 0);

        /* add tempq to flist */
        r = (p4est_quadrant_t *) sc_array_push (flist);
        *r = tempq;

        /* if tempq is a last sibling, go up a level */
        while (tempq.level >= minlevel && pid == P4EST_CHILDREN - 1) {
          p4est_quadrant_parent (&tempq, &tempp);
          tempq = tempp;
          pid = p4est_quadrant_child_id (&tempq);
        }

        /* if we've finished with all minlevel and smaller quadrants, we've
         * filled p */
        if (tempq.level < minlevel) {
          break;
        }

        /* get the next sibling */
        p4est_quadrant_sibling (&tempq, &tempq, ++pid);
      }

      /* if we've finished with all minlevel and smaller quadrants, we've
       * filled p */
      if (tempq.level < minlevel) {
        break;
      }

      P4EST_ASSERT (q != NULL);
      P4EST_ASSERT (p4est_quadrant_is_equal (&tempq, q) ||
                    p4est_quadrant_is_ancestor (&tempq, q));

      if (q->x == tempq.x && q->y == tempq.y &&
#ifdef P4_TO_P8
          q->z == tempq.z &&
#endif
          1) {
        /* if q is the first descendent of tempq, set tempq = q and get the next
         * q */
        if (q->level > tempq.level) {
          pid = 0;
        }
        tempq.level = q->level;
        jz++;
        if (jz < incount) {
          q = p4est_quadrant_array_index (inlist, jz);
          P4EST_ASSERT (p4est_quadrant_child_id (q) == 0);
        }
        else {
          q = NULL;
        }
      }
      else {
        /* get the largest first descendant of tempq that comes before
         * q */
        p4est_quadrant_first_descendant (&tempq, &tempp, P4EST_QMAXLEVEL);
        p4est_nearest_common_ancestor (&tempp, q, &tempq);
        tempq.level++;
        pid = 0;
        P4EST_ASSERT (p4est_quadrant_is_valid (&tempq));
        P4EST_ASSERT (p4est_quadrant_compare (&tempq, q) < 0);
        P4EST_ASSERT (!p4est_quadrant_is_ancestor (&tempq, q));
      }
    }

    P4EST_ASSERT (sc_array_is_sorted (flist, p4est_quadrant_compare));
    P4EST_ASSERT (flist->elem_count >= P4EST_CHILDREN);

#ifdef P4EST_DEBUG
    for (jz = 0; jz < flist->elem_count - 1; jz++) {
      q = p4est_quadrant_array_index (flist, jz);
      r = p4est_quadrant_array_index (flist, jz + 1);
      P4EST_ASSERT (p4est_quadrant_is_next (q, r));
    }
#endif

    /* we are going to put flist in tquadrants and remove p */
    num_this_added = flist->elem_count - 1;
    num_added += num_this_added;

    tqold = tquadrants->elem_count;
    sc_array_resize (tquadrants, tqold + num_this_added);

    /* move everything after p back */
    if (tqindex < tqold - 1) {
      memmove (sc_array_index (tquadrants, tqindex + num_this_added + 1),
               sc_array_index (tquadrants, tqindex + 1),
               tquadrants->elem_size * (tqold - 1 - tqindex));

    }
    tqoffset += num_this_added;
    /* tquadrants has resized, so we have to reinitialize tqview */
    sc_array_init_view (&tqview, tquadrants, tqoffset,
                        tquadrants->elem_count - tqoffset);

    /* first, remove p */
    q = sc_array_index (tquadrants, tqindex);

    P4EST_ASSERT (p4est_quadrant_is_equal (q, p));
    /* reset the data, decrement level count */
    p4est_quadrant_free_data (p4est, q);
    --tree->quadrants_per_level[q->level];

    /* copy findex into tquadrants */
    memcpy (sc_array_index (tquadrants, tqindex), sc_array_index (flist, 0),
            flist->elem_size * flist->elem_count);

    /* update counters, initialize data */
    for (jz = 0; jz < flist->elem_count; jz++) {
      q = sc_array_index (tquadrants, tqindex + jz);
      P4EST_ASSERT (p4est_quadrant_is_ancestor (p, q));
      ++tree->quadrants_per_level[q->level];
      tree->maxlevel = (int8_t) SC_MAX (tree->maxlevel, q->level);
      p4est_quadrant_init_data (p4est, which_tree, q, init_fn);
    }

    /* skip over the quadrants that we just operated on */
    iz = kz - 1;
  }

  sc_mempool_destroy (list_alloc);
  P4EST_ASSERT (tqorig + num_added == tquadrants->elem_count);

  /* print more statistics */
  P4EST_VERBOSEF
    ("Tree border %lld inlist %llu outlist %llu ancestor %llu insert %llu\n",
     (long long) which_tree, (unsigned long long) count_already_inlist,
     (unsigned long long) count_already_outlist,
     (unsigned long long) count_ancestor_inlist,
     (unsigned long long) num_added);

  sc_array_destroy (inlist);
  sc_array_destroy (flist);

  P4EST_ASSERT (p4est_tree_is_complete (tree));
}

void
p4est_complete_subtree (p4est_t * p4est,
                        p4est_topidx_t which_tree, p4est_init_t init_fn)
{
  p4est_complete_or_balance (p4est, which_tree, init_fn, 0);
}

void
p4est_balance_subtree (p4est_t * p4est, p4est_connect_type_t btype,
                       p4est_topidx_t which_tree, p4est_init_t init_fn)
{
  p4est_complete_or_balance (p4est, which_tree, init_fn,
                             p4est_connect_type_int (btype));
}

size_t
p4est_linearize_tree (p4est_t * p4est, p4est_tree_t * tree)
{
#ifdef P4EST_DEBUG
  size_t              data_pool_size;
#endif
  size_t              incount, removed;
  size_t              current, rest;
  p4est_locidx_t      num_quadrants;
  int                 i, maxlevel;
  p4est_quadrant_t   *q1, *q2;
  sc_array_t         *tquadrants = &tree->quadrants;

  P4EST_ASSERT (sc_array_is_sorted (tquadrants, p4est_quadrant_compare));

  incount = tquadrants->elem_count;
  if (incount <= 1) {
    return 0;
  }
#ifdef P4EST_DEBUG
  data_pool_size = 0;
  if (p4est->user_data_pool != NULL) {
    data_pool_size = p4est->user_data_pool->elem_count;
  }
#endif
  removed = 0;

  /* run through the array and remove ancestors */
  current = 0;
  rest = current + 1;
  q1 = p4est_quadrant_array_index (tquadrants, current);
  while (rest < incount) {
    q2 = p4est_quadrant_array_index (tquadrants, rest);
    if (p4est_quadrant_is_equal (q1, q2) ||
        p4est_quadrant_is_ancestor (q1, q2)) {
      --tree->quadrants_per_level[q1->level];
      p4est_quadrant_free_data (p4est, q1);
      *q1 = *q2;
      ++removed;
      ++rest;
    }
    else {
      ++current;
      if (current < rest) {
        q1 = p4est_quadrant_array_index (tquadrants, current);
        *q1 = *q2;
      }
      else {
        q1 = q2;
      }
      ++rest;
    }
  }

  /* resize array */
  sc_array_resize (tquadrants, current + 1);

  /* update level counters */
  maxlevel = 0;
  num_quadrants = 0;
  for (i = 0; i <= P4EST_QMAXLEVEL; ++i) {
    P4EST_ASSERT (tree->quadrants_per_level[i] >= 0);
    num_quadrants += tree->quadrants_per_level[i];      /* same type */
    if (tree->quadrants_per_level[i] > 0) {
      maxlevel = i;
    }
  }
  tree->maxlevel = (int8_t) maxlevel;

  /* sanity checks */
  P4EST_ASSERT (num_quadrants == (p4est_locidx_t) tquadrants->elem_count);
  P4EST_ASSERT (tquadrants->elem_count == incount - removed);
  if (p4est->user_data_pool != NULL) {
    P4EST_ASSERT (data_pool_size - removed ==
                  p4est->user_data_pool->elem_count);
  }
  P4EST_ASSERT (p4est_tree_is_sorted (tree));
  P4EST_ASSERT (p4est_tree_is_linear (tree));

  return removed;
}

p4est_locidx_t
p4est_partition_correction (p4est_gloidx_t * partition,
                            int num_procs, int rank,
                            p4est_gloidx_t min_quadrant_id,
                            p4est_gloidx_t max_quadrant_id)
{
  int                 i;
  int                 rank_with_max_quads = rank;
  p4est_gloidx_t      h;
  p4est_gloidx_t      max_num_quadrants =
    SC_MIN (max_quadrant_id, partition[rank + 1] - 1) - partition[rank] + 1;

  /* no correction if num quadrants not sufficient for family */
  if (max_quadrant_id - min_quadrant_id + 1 != P4EST_CHILDREN) {
    return 0;
  }

  /* decreasing search for process with highest amount of quadrants */
  i = rank_with_max_quads - 1;
  while (min_quadrant_id < partition[i + 1]) {
    h = partition[i + 1] - SC_MAX (min_quadrant_id, partition[i]);
    if (max_num_quadrants <= h) {
      max_num_quadrants = h;
      rank_with_max_quads = i;
    }
    i--;
  }

  /* increasing search for process with highest amount of quadrants */
  i = rank_with_max_quads + 1;
  while (partition[i] <= max_quadrant_id) {
    h = SC_MIN (max_quadrant_id, partition[i + 1] - 1) - partition[i] + 1;
    if (max_num_quadrants < h) {
      max_num_quadrants = h;
      rank_with_max_quads = i;
    }
    i++;
  }

  /* compute correction */
  if (rank_with_max_quads < rank) {
    return (p4est_locidx_t) (partition[rank] - max_quadrant_id - 1);
  }
  else {
    return (p4est_locidx_t) (partition[rank] - min_quadrant_id);
  }
}

int
p4est_next_nonempty_process (int rank, int num_procs,
                             p4est_locidx_t * num_quadrants_in_proc)
{
  if (rank >= num_procs) {      /* if `rank` is too high */
    /* return process id beyond scope */
    return num_procs;
  }

  /* search for next non empty process */
  while (rank < num_procs && num_quadrants_in_proc[rank] == 0) {
    rank++;
  }

  /* return non empty process id or last process id respectively */
  return rank;
}

p4est_gloidx_t
p4est_partition_given (p4est_t * p4est,
                       const p4est_locidx_t * new_num_quadrants_in_proc)
{
  const int           num_procs = p4est->mpisize;
  const int           rank = p4est->mpirank;
  const p4est_topidx_t first_local_tree = p4est->first_local_tree;
  const p4est_topidx_t last_local_tree = p4est->last_local_tree;
  const size_t        data_size = p4est->data_size;
  const size_t        quad_plus_data_size = sizeof (p4est_quadrant_t)
    + data_size;
  sc_array_t         *trees = p4est->trees;

  /* *INDENT-OFF* horrible indent bug */
  const p4est_topidx_t num_send_trees =
    p4est->global_first_position[rank + 1].p.which_tree - /* same type */
    p4est->global_first_position[rank].p.which_tree + 1;
  /* *INDENT-ON* */

  int                 i, sk;
  int                 from_proc, to_proc;
  int                 num_proc_recv_from, num_proc_send_to;
  char               *user_data_send_buf;
  char               *user_data_recv_buf;
  char              **recv_buf, **send_buf;
  size_t              recv_size, send_size, zz, zoffset;
  p4est_topidx_t      it;
  p4est_topidx_t      which_tree;
  p4est_topidx_t      first_tree, last_tree;
  p4est_topidx_t      num_recv_trees;
  p4est_topidx_t      new_first_local_tree, new_last_local_tree;
  p4est_topidx_t      first_from_tree, last_from_tree, from_tree;
  p4est_locidx_t      il;
  p4est_locidx_t      num_copy;
  p4est_locidx_t      num_quadrants;
  p4est_locidx_t      new_local_num_quadrants;
  p4est_locidx_t     *num_recv_from, *num_send_to;
  p4est_locidx_t     *new_local_tree_elem_count;
  p4est_locidx_t     *new_local_tree_elem_count_before;
  p4est_locidx_t     *num_per_tree_local;
  p4est_locidx_t     *num_per_tree_send_buf;
  p4est_locidx_t     *num_per_tree_recv_buf;
  p4est_gloidx_t     *begin_send_to;
  p4est_gloidx_t      tree_from_begin, tree_from_end, num_copy_global;
  p4est_gloidx_t      from_begin, from_end;
  p4est_gloidx_t      to_begin, to_end;
  p4est_gloidx_t      my_base, my_begin, my_end;
  p4est_gloidx_t     *global_last_quad_index;
  p4est_gloidx_t     *new_global_last_quad_index;
  p4est_gloidx_t     *local_tree_last_quad_index;
  p4est_gloidx_t      diff64, total_quadrants_shipped;
  sc_array_t         *quadrants;
  p4est_quadrant_t   *quad_send_buf;
  p4est_quadrant_t   *quad_recv_buf;
  p4est_quadrant_t   *quad;
  p4est_tree_t       *tree;
#ifdef P4EST_MPI
  int                 mpiret;
  MPI_Comm            comm = p4est->mpicomm;
  MPI_Request        *recv_request, *send_request;
  MPI_Status         *recv_status, *send_status;
#endif
#ifdef P4EST_DEBUG
  unsigned            crc;
  p4est_gloidx_t      total_requested_quadrants = 0;
#endif

  P4EST_GLOBAL_INFOF
    ("Into " P4EST_STRING "_partition_given with %lld total quadrants\n",
     (long long) p4est->global_num_quadrants);

#ifdef P4EST_DEBUG
  /* Save a checksum of the original forest */
  crc = p4est_checksum (p4est);
#endif

  /* Check for a valid requested partition and create last_quad_index */
  global_last_quad_index = P4EST_ALLOC (p4est_gloidx_t, num_procs);
  for (i = 0; i < num_procs; ++i) {
    global_last_quad_index[i] = p4est->global_first_quadrant[i + 1] - 1;
#ifdef P4EST_DEBUG
    total_requested_quadrants += new_num_quadrants_in_proc[i];
    P4EST_ASSERT (new_num_quadrants_in_proc[i] >= 0);
#endif
  }
  P4EST_ASSERT (total_requested_quadrants == p4est->global_num_quadrants);

  /* Print some diagnostics */
  if (rank == 0) {
    for (i = 0; i < num_procs; ++i) {
      P4EST_GLOBAL_LDEBUGF ("partition global_last_quad_index[%d] = %lld\n",
                            i, (long long) global_last_quad_index[i]);
    }
  }

  /* Calculate the global_last_quad_index for the repartitioned forest */
  new_global_last_quad_index = P4EST_ALLOC (p4est_gloidx_t, num_procs);
  new_global_last_quad_index[0] = new_num_quadrants_in_proc[0] - 1;
  for (i = 1; i < num_procs; ++i) {
    new_global_last_quad_index[i] = new_num_quadrants_in_proc[i] +
      new_global_last_quad_index[i - 1];
  }
  P4EST_ASSERT (global_last_quad_index[num_procs - 1] ==
                new_global_last_quad_index[num_procs - 1]);

  /* Calculate the global number of shipped (= received) quadrants */
  total_quadrants_shipped = 0;
  for (i = 1; i < num_procs; ++i) {
    diff64 =
      global_last_quad_index[i - 1] - new_global_last_quad_index[i - 1];
    if (diff64 >= 0) {
      total_quadrants_shipped +=
        SC_MIN (diff64, new_num_quadrants_in_proc[i]);
    }
    else {
      total_quadrants_shipped +=
        SC_MIN (-diff64, new_num_quadrants_in_proc[i - 1]);
    }
  }
  P4EST_ASSERT (0 <= total_quadrants_shipped &&
                total_quadrants_shipped <= p4est->global_num_quadrants);

  /* Print some diagnostics */
  if (rank == 0) {
    for (i = 0; i < num_procs; ++i) {
      P4EST_GLOBAL_LDEBUGF
        ("partition new_global_last_quad_index[%d] = %lld\n",
         i, (long long) new_global_last_quad_index[i]);
    }
  }

  /* Calculate the local index of the end of each tree */
  local_tree_last_quad_index =
    P4EST_ALLOC_ZERO (p4est_gloidx_t, trees->elem_count);
  if (first_local_tree >= 0) {
    tree = p4est_tree_array_index (trees, first_local_tree);
    local_tree_last_quad_index[first_local_tree]
      = tree->quadrants.elem_count - 1;
  }
  else {
    /* empty processor */
    P4EST_ASSERT (first_local_tree == -1 && last_local_tree == -2);
  }
  for (which_tree = first_local_tree + 1;       /* same type */
       which_tree <= last_local_tree; ++which_tree) {
    tree = p4est_tree_array_index (trees, which_tree);
    local_tree_last_quad_index[which_tree] = tree->quadrants.elem_count
      + local_tree_last_quad_index[which_tree - 1];
  }

#ifdef P4EST_DEBUG
  for (which_tree = first_local_tree; which_tree <= last_local_tree;
       ++which_tree) {
    tree = p4est_tree_array_index (trees, which_tree);
    P4EST_LDEBUGF
      ("partition tree %lld local_tree_last_quad_index[%lld] = %lld\n",
       (long long) which_tree, (long long) which_tree,
       (long long) local_tree_last_quad_index[which_tree]);
  }
#endif

  /* Calculate where the new quadrants are coming from */
  num_recv_from = P4EST_ALLOC_ZERO (p4est_locidx_t, num_procs);

  my_begin = (rank == 0) ? 0 : (new_global_last_quad_index[rank - 1] + 1);
  my_end = new_global_last_quad_index[rank];

  num_proc_recv_from = 0;
  for (from_proc = 0; from_proc < num_procs; ++from_proc) {
    from_begin = (from_proc == 0) ?
      0 : (global_last_quad_index[from_proc - 1] + 1);
    from_end = global_last_quad_index[from_proc];

    if (from_begin <= my_end && from_end >= my_begin) {
      /* from_proc sends to me but may be empty */
      num_recv_from[from_proc] = SC_MIN (my_end, from_end)
        - SC_MAX (my_begin, from_begin) + 1;
      P4EST_ASSERT (num_recv_from[from_proc] >= 0);
      if (from_proc != rank)
        ++num_proc_recv_from;
    }
  }

#ifdef P4EST_DEBUG
  for (i = 0; i < num_procs; ++i) {
    if (num_recv_from[i] != 0) {
      P4EST_LDEBUGF ("partition num_recv_from[%d] = %lld\n", i,
                     (long long) num_recv_from[i]);
    }
  }
#endif

  /* Post receives for the quadrants and their data */
  recv_buf = P4EST_ALLOC_ZERO (char *, num_procs);
#ifdef P4EST_MPI
  recv_request = P4EST_ALLOC (MPI_Request, num_proc_recv_from);
  recv_status = P4EST_ALLOC (MPI_Status, num_proc_recv_from);
#endif

  /* Allocate space for receiving quadrants and user data */
  for (from_proc = 0, sk = 0; from_proc < num_procs; ++from_proc) {
    if (from_proc != rank && num_recv_from[from_proc] > 0) {
      num_recv_trees =          /* same type */
        p4est->global_first_position[from_proc + 1].p.which_tree
        - p4est->global_first_position[from_proc].p.which_tree + 1;
      recv_size = num_recv_trees * sizeof (p4est_locidx_t)
        + quad_plus_data_size * num_recv_from[from_proc];

      recv_buf[from_proc] = P4EST_ALLOC (char, recv_size);

      /* Post receives for the quadrants and their data */
#ifdef P4EST_MPI
      P4EST_LDEBUGF ("partition recv %lld quadrants from %d\n",
                     (long long) num_recv_from[from_proc], from_proc);
      mpiret = MPI_Irecv (recv_buf[from_proc], (int) recv_size, MPI_BYTE,
                          from_proc, P4EST_COMM_PARTITION_GIVEN,
                          comm, recv_request + sk);
      SC_CHECK_MPI (mpiret);
#endif
      ++sk;
    }
  }
#ifdef P4EST_MPI
  for (; sk < num_proc_recv_from; ++sk) {
    /* for empty processors in receiving range */
    recv_request[sk] = MPI_REQUEST_NULL;
  }
#endif

  /* For each processor calculate the number of quadrants sent */
  num_send_to = P4EST_ALLOC (p4est_locidx_t, num_procs);
  begin_send_to = P4EST_ALLOC (p4est_gloidx_t, num_procs);

  my_begin = (rank == 0) ? 0 : (global_last_quad_index[rank - 1] + 1);
  my_end = global_last_quad_index[rank];

  num_proc_send_to = 0;
  for (to_proc = 0; to_proc < num_procs; ++to_proc) {
    to_begin = (to_proc == 0)
      ? 0 : (new_global_last_quad_index[to_proc - 1] + 1);
    to_end = new_global_last_quad_index[to_proc];

    if (to_begin <= my_end && to_end >= my_begin) {
      /* I send to to_proc which may be empty */
      num_send_to[to_proc] = SC_MIN (my_end, to_end)
        - SC_MAX (my_begin, to_begin) + 1;
      begin_send_to[to_proc] = SC_MAX (my_begin, to_begin);
      P4EST_ASSERT (num_send_to[to_proc] >= 0);
      if (to_proc != rank)
        ++num_proc_send_to;
    }
    else {
      /* I don't send to to_proc */
      num_send_to[to_proc] = 0;
      begin_send_to[to_proc] = -1;
    }
  }

#ifdef P4EST_DEBUG
  for (i = 0; i < num_procs; ++i) {
    if (num_send_to[i] != 0) {
      P4EST_LDEBUGF ("partition num_send_to[%d] = %lld\n",
                     i, (long long) num_send_to[i]);
    }
  }
  for (i = 0; i < num_procs; ++i) {
    if (begin_send_to[i] != -1) {
      P4EST_LDEBUGF ("partition begin_send_to[%d] = %lld\n",
                     i, (long long) begin_send_to[i]);
    }
  }
#endif

  /* Communicate the quadrants and their data */
  send_buf = P4EST_ALLOC (char *, num_procs);
#ifdef P4EST_MPI
  send_request = P4EST_ALLOC (MPI_Request, num_proc_send_to);
  send_status = P4EST_ALLOC (MPI_Status, num_proc_send_to);
#endif

  /* Set the num_per_tree_local */
  num_per_tree_local = P4EST_ALLOC_ZERO (p4est_locidx_t, num_send_trees);
  to_proc = rank;
  my_base = (rank == 0) ? 0 : (global_last_quad_index[rank - 1] + 1);
  my_begin = begin_send_to[to_proc] - my_base;
  my_end = begin_send_to[to_proc] + num_send_to[to_proc] - 1 - my_base;
  for (which_tree = first_local_tree; which_tree <= last_local_tree;
       ++which_tree) {
    tree = p4est_tree_array_index (trees, which_tree);

    from_begin = (which_tree == first_local_tree) ? 0 :
      (local_tree_last_quad_index[which_tree - 1] + 1);
    from_end = local_tree_last_quad_index[which_tree];

    if (from_begin <= my_end && from_end >= my_begin) {
      /* Need to copy from tree which_tree */
      tree_from_begin = SC_MAX (my_begin, from_begin) - from_begin;
      tree_from_end = SC_MIN (my_end, from_end) - from_begin;
      num_copy_global = tree_from_end - tree_from_begin + 1;
      P4EST_ASSERT (num_copy_global >= 0);
      P4EST_ASSERT (num_copy_global <= (p4est_gloidx_t) P4EST_LOCIDX_MAX);
      num_copy = (p4est_locidx_t) num_copy_global;
      num_per_tree_local[which_tree - first_local_tree] = num_copy;
    }
  }

  /* Allocate space for receiving quadrants and user data */
  for (to_proc = 0, sk = 0; to_proc < num_procs; ++to_proc) {
    if (to_proc != rank && num_send_to[to_proc]) {
      send_size = num_send_trees * sizeof (p4est_locidx_t)
        + quad_plus_data_size * num_send_to[to_proc];

      send_buf[to_proc] = P4EST_ALLOC (char, send_size);

      num_per_tree_send_buf = (p4est_locidx_t *) send_buf[to_proc];
      memset (num_per_tree_send_buf, 0,
              num_send_trees * sizeof (p4est_locidx_t));
      quad_send_buf = (p4est_quadrant_t *) (send_buf[to_proc]
                                            +
                                            num_send_trees *
                                            sizeof (p4est_locidx_t));
      user_data_send_buf =
        send_buf[to_proc] + num_send_trees * sizeof (p4est_locidx_t)
        + num_send_to[to_proc] * sizeof (p4est_quadrant_t);

      /* Pack in the data to be sent */

      my_base = (rank == 0) ? 0 : (global_last_quad_index[rank - 1] + 1);
      my_begin = begin_send_to[to_proc] - my_base;
      my_end = begin_send_to[to_proc] + num_send_to[to_proc] - 1 - my_base;

      for (which_tree = first_local_tree; which_tree <= last_local_tree;
           ++which_tree) {
        tree = p4est_tree_array_index (trees, which_tree);

        from_begin = (which_tree == first_local_tree) ? 0 :
          (local_tree_last_quad_index[which_tree - 1] + 1);
        from_end = local_tree_last_quad_index[which_tree];

        if (from_begin <= my_end && from_end >= my_begin) {
          /* Need to copy from tree which_tree */
          tree_from_begin = SC_MAX (my_begin, from_begin) - from_begin;
          tree_from_end = SC_MIN (my_end, from_end) - from_begin;
          num_copy = tree_from_end - tree_from_begin + 1;

          num_per_tree_send_buf[which_tree - first_local_tree] = num_copy;

          /* copy quads to send buf */
          memcpy (quad_send_buf, tree->quadrants.array +
                  tree_from_begin * sizeof (p4est_quadrant_t),
                  num_copy * sizeof (p4est_quadrant_t));

          /* set tree in send buf and copy user data */
          P4EST_LDEBUGF ("partition send %lld [%lld,%lld] quadrants"
                         " from tree %lld to proc %d\n",
                         (long long) num_copy, (long long) tree_from_begin,
                         (long long) tree_from_end, (long long) which_tree,
                         to_proc);
          for (il = 0; il < num_copy; ++il) {
            memcpy (user_data_send_buf + il * data_size,
                    quad_send_buf[il].p.user_data, data_size);
            quad_send_buf[il].p.user_data = NULL;
          }

          /* move pointer to beginning of quads that need to be copied */
          my_begin += num_copy;
          quad_send_buf += num_copy;
          user_data_send_buf += num_copy * data_size;
        }
      }

      /* Post send operation for the quadrants and their data */
#ifdef P4EST_MPI
      P4EST_LDEBUGF ("partition send %lld quadrants to %d\n",
                     (long long) num_send_to[to_proc], to_proc);
      mpiret = MPI_Isend (send_buf[to_proc], (int) send_size, MPI_BYTE,
                          to_proc, P4EST_COMM_PARTITION_GIVEN,
                          comm, send_request + sk);
      SC_CHECK_MPI (mpiret);
      ++sk;
#endif
    }
    else {
      send_buf[to_proc] = NULL;
    }
  }
#ifdef P4EST_MPI
  for (; sk < num_proc_send_to; ++sk) {
    send_request[sk] = MPI_REQUEST_NULL;
  }

  /* Fill in forest */
  mpiret = MPI_Waitall (num_proc_recv_from, recv_request, recv_status);
  SC_CHECK_MPI (mpiret);
#endif

  /* Loop through and fill in */

  /* Calculate the local index of the end of each tree in the repartition */
  new_local_tree_elem_count =
    P4EST_ALLOC_ZERO (p4est_locidx_t, trees->elem_count);
  new_local_tree_elem_count_before =
    P4EST_ALLOC_ZERO (p4est_locidx_t, trees->elem_count);
  new_first_local_tree = (p4est_topidx_t) P4EST_TOPIDX_MAX;
  new_last_local_tree = 0;

  for (from_proc = 0; from_proc < num_procs; ++from_proc) {
    if (num_recv_from[from_proc] > 0) {
      first_from_tree = p4est->global_first_position[from_proc].p.which_tree;
      last_from_tree =
        p4est->global_first_position[from_proc + 1].p.which_tree;
      num_recv_trees =          /* same type */
        last_from_tree - first_from_tree + 1;

      P4EST_LDEBUGF
        ("partition from %d with trees [%lld,%lld] get %lld trees\n",
         from_proc, (long long) first_from_tree, (long long) last_from_tree,
         (long long) num_recv_trees);
      num_per_tree_recv_buf = (from_proc == rank) ?
        num_per_tree_local : (p4est_locidx_t *) recv_buf[from_proc];

      for (it = 0; it < num_recv_trees; ++it) {

        if (num_per_tree_recv_buf[it] > 0) {
          from_tree = first_from_tree + it;     /* same type */

          P4EST_ASSERT (from_tree >= 0
                        && from_tree < (p4est_topidx_t) trees->elem_count);
          P4EST_LDEBUGF ("partition recv %lld [%lld,%lld] quadrants"
                         " from tree %lld from proc %d\n",
                         (long long) num_per_tree_recv_buf[it],
                         (long long) new_local_tree_elem_count[from_tree],
                         (long long) new_local_tree_elem_count[from_tree]
                         + num_per_tree_recv_buf[it], (long long) from_tree,
                         from_proc);
          new_first_local_tree =        /* same type */
            SC_MIN (new_first_local_tree, from_tree);
          new_last_local_tree = /* same type */
            SC_MAX (new_last_local_tree, from_tree);
          new_local_tree_elem_count[from_tree] +=       /* same type */
            num_per_tree_recv_buf[it];
          new_local_tree_elem_count_before[from_tree] +=        /* same type */
            (from_proc < rank) ? num_per_tree_recv_buf[it] : 0;
        }
      }
    }
  }
  if (new_first_local_tree > new_last_local_tree) {
    new_first_local_tree = -1;
    new_last_local_tree = -2;
  }
  P4EST_VERBOSEF ("partition new forest [%lld,%lld]\n",
                  (long long) new_first_local_tree,
                  (long long) new_last_local_tree);

  /* Copy the local quadrants */
  if (first_local_tree >= 0 && new_first_local_tree >= 0) {
    P4EST_ASSERT (last_local_tree >= 0 && new_last_local_tree >= 0);
    first_tree =                /* same type */
      SC_MIN (first_local_tree, new_first_local_tree);
  }
  else {
    P4EST_ASSERT (last_local_tree == -2 || new_last_local_tree == -2);
    first_tree =                /* same type */
      SC_MAX (first_local_tree, new_first_local_tree);
  }
  last_tree =                   /* same type */
    SC_MAX (last_local_tree, new_last_local_tree);
  my_base = (rank == 0) ? 0 : (global_last_quad_index[rank - 1] + 1);
  my_begin = begin_send_to[rank] - my_base;
  my_end = begin_send_to[rank] + num_send_to[rank] - 1 - my_base;

  for (which_tree = first_tree; which_tree <= last_tree; ++which_tree) {
    tree = p4est_tree_array_index (trees, which_tree);
    quadrants = &tree->quadrants;

    if (new_local_tree_elem_count[which_tree] > 0) {
      if (which_tree >= first_local_tree && which_tree <= last_local_tree) {

        num_quadrants = new_local_tree_elem_count[which_tree];

        from_begin = (which_tree == first_local_tree) ? 0 :
          (local_tree_last_quad_index[which_tree - 1] + 1);
        from_end = local_tree_last_quad_index[which_tree];

        if (from_begin <= my_end && from_end >= my_begin) {
          /* Need to keep part of tree which_tree */
          tree_from_begin = SC_MAX (my_begin, from_begin) - from_begin;
          tree_from_end = SC_MIN (my_end, from_end) - from_begin;
          num_copy = tree_from_end - tree_from_begin + 1;
        }
        else {
          tree_from_begin = 0;
          tree_from_end = -1;
          num_copy = 0;
        }

        /* Free all userdata that no longer belongs to this process */
        zoffset = SC_MIN ((size_t) tree_from_begin, quadrants->elem_count);
        for (zz = 0; zz < zoffset; ++zz) {
          quad = p4est_quadrant_array_index (quadrants, zz);
          p4est_quadrant_free_data (p4est, quad);
        }
        zoffset = (size_t) tree_from_end + 1;
        for (zz = zoffset; zz < quadrants->elem_count; ++zz) {
          quad = p4est_quadrant_array_index (quadrants, zz);
          p4est_quadrant_free_data (p4est, quad);
        }

        if (num_quadrants > (p4est_locidx_t) quadrants->elem_count) {
          sc_array_resize (quadrants, num_quadrants);
        }

        P4EST_LDEBUGF ("copying %lld local quads to tree %lld\n",
                       (long long) num_copy, (long long) which_tree);
        P4EST_LDEBUGF
          ("   with %lld(%llu) quads from [%lld, %lld] to [%lld, %lld]\n",
           (long long) num_quadrants,
           (unsigned long long) quadrants->elem_count,
           (long long) tree_from_begin, (long long) tree_from_end,
           (long long) new_local_tree_elem_count_before[which_tree],
           (long long) new_local_tree_elem_count_before[which_tree] +
           num_copy - 1);
        memmove (quadrants->array +
                 new_local_tree_elem_count_before[which_tree] *
                 sizeof (p4est_quadrant_t),
                 quadrants->array +
                 tree_from_begin * sizeof (p4est_quadrant_t),
                 num_copy * sizeof (p4est_quadrant_t));

        if (num_quadrants < (p4est_locidx_t) quadrants->elem_count) {
          sc_array_resize (quadrants, num_quadrants);
        }
      }
    }
    else {
      /*
       * Check to see if we need to drop a tree because we no longer have
       * any quadrants in it.
       */
      if (which_tree >= first_local_tree && which_tree <= last_local_tree) {
        /* Free all userdata that no longer belongs to this process */
        for (zz = 0; zz < quadrants->elem_count; ++zz) {
          quad = p4est_quadrant_array_index (quadrants, zz);
          p4est_quadrant_free_data (p4est, quad);
        }

        /* The whole tree is dropped */
        P4EST_QUADRANT_INIT (&tree->first_desc);
        P4EST_QUADRANT_INIT (&tree->last_desc);
        sc_array_reset (quadrants);
        tree->quadrants_offset = 0;
        for (i = 0; i <= P4EST_QMAXLEVEL; ++i) {
          tree->quadrants_per_level[i] = 0;
        }
        tree->maxlevel = 0;
      }
    }
  }

  /* Copy in received quadrants */

  memset (new_local_tree_elem_count_before, 0,
          trees->elem_count * sizeof (p4est_locidx_t));
  for (from_proc = 0; from_proc < num_procs; ++from_proc) {
    if (num_recv_from[from_proc] > 0) {
      first_from_tree = p4est->global_first_position[from_proc].p.which_tree;
      last_from_tree =
        p4est->global_first_position[from_proc + 1].p.which_tree;
      num_recv_trees =          /* same type */
        last_from_tree - first_from_tree + 1;

      P4EST_LDEBUGF
        ("partition copy from %d with trees [%lld,%lld] get %lld trees\n",
         from_proc, (long long) first_from_tree,
         (long long) last_from_tree, (long long) num_recv_trees);
      num_per_tree_recv_buf =
        (from_proc == rank) ? num_per_tree_local :
        (p4est_locidx_t *) recv_buf[from_proc];

      quad_recv_buf = (p4est_quadrant_t *) (recv_buf[from_proc]
                                            + num_recv_trees *
                                            sizeof (p4est_locidx_t));
      user_data_recv_buf =
        recv_buf[from_proc] + num_recv_trees * sizeof (p4est_locidx_t)
        + num_recv_from[from_proc] * sizeof (p4est_quadrant_t);

      for (it = 0; it < num_recv_trees; ++it) {
        from_tree = first_from_tree + it;       /* same type */
        num_copy = num_per_tree_recv_buf[it];

        /* We might have sent trees that are not actual trees.  In
         * this case the num_copy should be zero
         */
        P4EST_ASSERT (num_copy == 0
                      || (num_copy > 0 && from_tree >= 0
                          && from_tree < (p4est_topidx_t) trees->elem_count));

        if (num_copy > 0 && rank != from_proc) {
          tree = p4est_tree_array_index (trees, from_tree);
          quadrants = &tree->quadrants;
          num_quadrants = new_local_tree_elem_count[from_tree];
          sc_array_resize (quadrants, num_quadrants);

          /* copy quadrants */
          P4EST_LDEBUGF ("copying %lld remote quads to tree %lld"
                         " with %lld quads from proc %d\n",
                         (long long) num_copy, (long long) from_tree,
                         (long long) num_quadrants, from_proc);
          memcpy (quadrants->array +
                  new_local_tree_elem_count_before[from_tree]
                  * sizeof (p4est_quadrant_t), quad_recv_buf,
                  num_copy * sizeof (p4est_quadrant_t));

          /* copy user data */
          zoffset = (size_t) new_local_tree_elem_count_before[from_tree];
          for (zz = 0; zz < (size_t) num_copy; ++zz) {
            quad = p4est_quadrant_array_index (quadrants, zz + zoffset);

            if (data_size > 0) {
              quad->p.user_data = sc_mempool_alloc (p4est->user_data_pool);
              memcpy (quad->p.user_data, user_data_recv_buf + zz * data_size,
                      data_size);
            }
            else {
              quad->p.user_data = NULL;
            }
          }
        }

        if (num_copy > 0) {
          P4EST_ASSERT (from_tree >= 0
                        && from_tree < (p4est_topidx_t) trees->elem_count);
          new_local_tree_elem_count_before[from_tree] +=        /* same type */
            num_copy;
        }

        /* increment the recv quadrant pointers */
        quad_recv_buf += num_copy;
        user_data_recv_buf += num_copy * data_size;
      }
      if (recv_buf[from_proc] != NULL) {
        P4EST_FREE (recv_buf[from_proc]);
        recv_buf[from_proc] = NULL;
      }
    }
  }

  /* Set the global index and count of quadrants instead
   * of calling p4est_comm_count_quadrants
   */
  P4EST_FREE (global_last_quad_index);
  global_last_quad_index = new_global_last_quad_index;
  P4EST_ASSERT (p4est->global_num_quadrants ==
                new_global_last_quad_index[num_procs - 1] + 1);
  P4EST_ASSERT (p4est->global_first_quadrant[0] == 0);
  for (i = 0; i < num_procs; ++i) {
    p4est->global_first_quadrant[i + 1] = global_last_quad_index[i] + 1;
  }
  P4EST_FREE (new_global_last_quad_index);
  global_last_quad_index = new_global_last_quad_index = NULL;

  p4est->first_local_tree = new_first_local_tree;
  p4est->last_local_tree = new_last_local_tree;

  new_local_num_quadrants = 0;
  for (which_tree = 0; which_tree < new_first_local_tree; ++which_tree) {
    tree = p4est_tree_array_index (trees, which_tree);
    tree->quadrants_offset = 0;
    P4EST_QUADRANT_INIT (&tree->first_desc);
    P4EST_QUADRANT_INIT (&tree->last_desc);
  }
  for (; which_tree <= new_last_local_tree; ++which_tree) {
    tree = p4est_tree_array_index (trees, which_tree);
    tree->quadrants_offset = new_local_num_quadrants;
    quadrants = &tree->quadrants;
    P4EST_ASSERT (quadrants->elem_count > 0);

    new_local_num_quadrants +=  /* same type */
      (p4est_locidx_t) quadrants->elem_count;

    for (i = 0; i <= P4EST_QMAXLEVEL; ++i) {
      tree->quadrants_per_level[i] = 0;
    }
    tree->maxlevel = 0;
    for (zz = 0; zz < quadrants->elem_count; ++zz) {
      quad = p4est_quadrant_array_index (quadrants, zz);
      ++tree->quadrants_per_level[quad->level];
      tree->maxlevel = (int8_t) SC_MAX (quad->level, tree->maxlevel);
    }

    quad = p4est_quadrant_array_index (quadrants, 0);
    p4est_quadrant_first_descendant (quad, &tree->first_desc,
                                     P4EST_QMAXLEVEL);
    quad = p4est_quadrant_array_index (quadrants, quadrants->elem_count - 1);
    p4est_quadrant_last_descendant (quad, &tree->last_desc, P4EST_QMAXLEVEL);
  }
  for (; which_tree < p4est->connectivity->num_trees; ++which_tree) {
    tree = p4est_tree_array_index (trees, which_tree);
    tree->quadrants_offset = new_local_num_quadrants;
    P4EST_QUADRANT_INIT (&tree->first_desc);
    P4EST_QUADRANT_INIT (&tree->last_desc);
  }
  p4est->local_num_quadrants = new_local_num_quadrants;

  /* Clean up */

#ifdef P4EST_MPI
  mpiret = MPI_Waitall (num_proc_send_to, send_request, send_status);
  SC_CHECK_MPI (mpiret);

#ifdef P4EST_DEBUG
  for (i = 0; i < num_proc_recv_from; ++i) {
    P4EST_ASSERT (recv_request[i] == MPI_REQUEST_NULL);
  }
  for (i = 0; i < num_proc_send_to; ++i) {
    P4EST_ASSERT (send_request[i] == MPI_REQUEST_NULL);
  }
#endif
  P4EST_FREE (recv_request);
  P4EST_FREE (recv_status);
  P4EST_FREE (send_request);
  P4EST_FREE (send_status);
#endif

  for (i = 0; i < num_procs; ++i) {
    if (recv_buf[i] != NULL)
      P4EST_FREE (recv_buf[i]);
    if (send_buf[i] != NULL)
      P4EST_FREE (send_buf[i]);
  }

  P4EST_FREE (num_per_tree_local);
  P4EST_FREE (local_tree_last_quad_index);
  P4EST_FREE (new_local_tree_elem_count);
  P4EST_FREE (new_local_tree_elem_count_before);
  P4EST_FREE (recv_buf);
  P4EST_FREE (send_buf);
  P4EST_FREE (num_recv_from);
  P4EST_FREE (num_send_to);
  P4EST_FREE (begin_send_to);

  p4est_comm_global_partition (p4est, NULL);

  /* Assert that we have a valid partition */
  P4EST_ASSERT (crc == p4est_checksum (p4est));
  P4EST_GLOBAL_INFOF
    ("Done " P4EST_STRING
     "_partition_given shipped %lld quadrants %.3g%%\n",
     (long long) total_quadrants_shipped,
     total_quadrants_shipped * 100. / p4est->global_num_quadrants);

  return total_quadrants_shipped;
}
