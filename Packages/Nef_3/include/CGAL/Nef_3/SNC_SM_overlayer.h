// Copyright (c) 1997-2000  Max-Planck-Institute Saarbruecken (Germany).
// All rights reserved.
//
// This file is part of CGAL (www.cgal.org); you may redistribute it under
// the terms of the Q Public License version 1.0.
// See the file LICENSE.QPL distributed with CGAL.
//
// Licensees holding a valid commercial license may use this file in
// accordance with the commercial license agreement provided with the software.
//
// This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
// WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
//
// $Source$
// $Revision$ $Date$
// $Name$
//
// Author(s)     : Michael Seel <seel@mpi-sb.mpg.de>
//                 Peter Hachenberger <hachenberger@mpi-sb.mpg.de>

#ifndef CGAL_SNC_SM_OVERLAYER_H
#define CGAL_SNC_SM_OVERLAYER_H

#include <CGAL/basic.h>
#include <CGAL/Union_find.h>
#include <CGAL/Nef_2/Segment_overlay_traits.h>
#include <CGAL/Nef_2/geninfo.h>
#include <CGAL/Nef_S2/Sphere_geometry.h>
#include <CGAL/Nef_S2/SM_decorator.h>
#include <CGAL/Nef_S2/SM_const_decorator.h>
#include <CGAL/Nef_S2/SM_overlayer.h>
#include <CGAL/Nef_3/SNC_structure.h>
#undef CGAL_NEF_DEBUG
#define CGAL_NEF_DEBUG 131
#include <CGAL/Nef_2/debug.h>

#ifndef CGAL_USE_LEDA
#define LEDA_MEMORY(t) 
#endif

CGAL_BEGIN_NAMESPACE

/*{\Manpage {SNC_SM_overlayer}{Refs_}{Overlay in the sphere}{O}}*/

template <typename SM_decorator_>
class SNC_SM_overlayer : public SM_overlayer<SM_decorator_> {
public:

  typedef SM_decorator_                         SM_decorator;
  typedef typename SM_decorator::Map            Map;
  typedef SM_overlayer<SM_decorator_>           Base;
  typedef SNC_SM_overlayer<SM_decorator_>       Self;

  //  typedef typename Base::Constructor_parameter Constructor_parameter;
  typedef typename Base::SVertex_handle SVertex_handle;
  typedef typename Base::SHalfedge_handle SHalfedge_handle;
  typedef typename Base::SHalfloop_handle SHalfloop_handle;
  typedef typename Base::SFace_handle SFace_handle;
  typedef typename Base::SVertex_iterator SVertex_iterator;
  typedef typename Base::SHalfedge_iterator SHalfedge_iterator;
  typedef typename Base::SFace_iterator SFace_iterator;
  typedef typename Base::SHalfedge_around_sface_circulator 
                         SHalfedge_around_sface_circulator;

  typedef typename Base::Sphere_kernel           Sphere_kernel;

  typedef typename Map::Infi_box Infi_box;

 public:
  void simplify();

  SNC_SM_overlayer(Map* M, 
    const Sphere_kernel& G = Sphere_kernel()) : Base(M,G) {}
};

template <typename Map>
void SNC_SM_overlayer<Map>::simplify()
{
  CGAL_NEF_TRACEN("simplifying"); 

  typedef typename CGAL::Union_find<SFace_handle>::handle Union_find_handle;
  CGAL::Unique_hash_map< SFace_handle, Union_find_handle> Pitem(NULL);
  CGAL::Unique_hash_map< SVertex_handle, Union_find_handle> Vitem(NULL);
  CGAL::Union_find< SFace_handle> UF;
  
  SFace_iterator f;
  CGAL_forall_sfaces(f,*this) {
     Pitem[f] = UF.make_set(f);
     clear_face_cycle_entries(f);
  }

  if ( this->has_shalfloop() ) {
    SHalfloop_handle l = this->shalfloop();
    SFace_handle f = *(UF.find(Pitem[face(l)]));
    link_as_loop(l,f);
    f = *(UF.find(Pitem[face(twin(l))]));
    link_as_loop(twin(l),f);
  }

  SHalfedge_iterator e, en;
  for(e = this->shalfedges_begin(); e != this->shalfedges_end(); e = en) { 
    en = e; ++en; if ( en==twin(e) ) ++en;
    CGAL_NEF_TRACEN("can simplify ? " << PH(e));
    if(!Infi_box::is_sedge_on_infibox(e)) {
      CGAL_NEF_TRACEN(mark(e) << " " << mark(face(e)) << " " << mark(face(twin(e))));
      if (( mark(e) == mark(face(e)) && mark(e) == mark(face(twin(e))))){
	CGAL_NEF_TRACEN("deleting "<<PH(e));
	if ( !UF.same_set(Pitem[face(e)],
			  Pitem[face(twin(e))]) ) {
	  
	  UF.unify_sets( Pitem[face(e)],
			 Pitem[face(twin(e))] );
	  CGAL_NEF_TRACEN("unioning disjoint faces");
	}
	
	CGAL_NEF_TRACEN("is_closed_at_source " << is_closed_at_source(e) << 
	       " " << is_closed_at_source(twin(e)));
	
	if ( is_closed_at_source(e) )
	  Vitem[source(e)] = Pitem[face(e)];
      
	if ( is_closed_at_source(twin(e)))
	  Vitem[target(e)] = Pitem[face(e)];
     
	delete_edge_pair(e);
      }
    }
  }

  CGAL::Unique_hash_map<SHalfedge_handle,bool> linked(false);
  for (e = this->shalfedges_begin(); e != this->shalfedges_end(); ++e) {
    if ( linked[e] ) continue;
    SHalfedge_around_sface_circulator hfc(e),hend(hfc);
    SFace_handle f = *(UF.find( Pitem[face(e)]));
    CGAL_For_all(hfc,hend) {  set_face(hfc,f); linked[hfc]=true; }
    store_sm_boundary_object(e,f);
  }

  SVertex_iterator v,vn;
  for(v = this->svertices_begin(); v != this->svertices_end(); v=vn) {
    vn=v; ++vn;
    if ( is_isolated(v) ) {
    
      if(Vitem[v] != NULL) {
	set_face(v,*(UF.find(Vitem[v])));
	CGAL_NEF_TRACEN("incident face of " << PH(v) << " set to " << &*(face(v)));
      }
      else {
	set_face(v, *(UF.find(Pitem[face(v)])));
	CGAL_NEF_TRACEN("isolated svertex " << PH(v) << 
	       " already has incident face " << &*(face(v)));
      }

      if ( mark(v) == mark(face(v)) ) {
        CGAL_NEF_TRACEN("removing isolated vertex"<<PH(v));
        delete_vertex_only(v);  
      } 
      else 
        store_sm_boundary_object(v,face(v)); // isolated, but should stay
    } else { // v not isolated
      SHalfedge_handle e2 = first_out_edge(v), e1 = previous(e2);
      if ( has_outdeg_two(v) &&
           mark(v) == mark(e1) && mark(v) == mark(e2) &&
           circle(e1) == circle(e2) ) {
        CGAL_NEF_TRACEN("collinear at "<<PH(v)<<PH(e1)<<PH(e2));
        if ( e1 == e2 ){ CGAL_NEF_TRACEN("edge_to_loop"); convert_edge_to_loop(e1);}
        else {CGAL_NEF_TRACEN("merge_edge_pairs"); merge_edge_pairs_at_target(e1); } 
      }
    }
  }

  SFace_iterator fn;
  for (f = fn = this->sfaces_begin(); f != this->sfaces_end(); f=fn) { 
    ++fn;
    Union_find_handle pit = Pitem[f];
    if ( UF.find(pit) != pit ) {
      CGAL_NEF_TRACEN("delete face " << &*f);
      delete_face_only(f);
    }
  }

  CGAL_NEF_TRACEN(" ");
  CGAL_NEF_TRACEN("resulting vertex ");
    
  CGAL_forall_svertices(vn, *this)
    CGAL_NEF_TRACEN("|" << vn->point() << "|" << vn->mark());
  CGAL_NEF_TRACEN(" ");
  
  CGAL_forall_shalfedges(en,*this)
    CGAL_NEF_TRACEN("|" << en->circle() <<
	   "|" << en->mark() << 
	   " " << en->incident_sface()->mark());
  CGAL_NEF_TRACEN("---------------------");

}

CGAL_END_NAMESPACE
#endif //CGAL_SNC_SM_OVERLAYER_H


