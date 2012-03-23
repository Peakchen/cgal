#include <boost/lexical_cast.hpp>
#include <boost/math/special_functions/round.hpp>

#include <iostream>
#include <fstream>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Simple_cartesian.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits.h>
#include <CGAL/Polyhedron_3.h>
#include <CGAL/IO/Polyhedron_iostream.h>
#include <CGAL/AABB_polyhedron_triangle_primitive.h>
#include <CGAL/Random.h>
#include <CGAL/point_generators_3.h>
#include <CGAL/Timer.h>
#include <boost/call_traits.hpp>
#include <boost/logic/tribool.hpp>

typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
//typedef CGAL::Simple_cartesian<double> K;
typedef K::Point_3 Point;
typedef K::Plane_3 Plane;
typedef K::Vector_3 Vector;
typedef K::Segment_3 Segment;
typedef K::Ray_3 Ray;
typedef CGAL::Polyhedron_3<K> Polyhedron;
typedef CGAL::AABB_polyhedron_triangle_primitive<K,Polyhedron> Primitive;
typedef CGAL::AABB_traits<K, Primitive> Traits;
typedef Traits::Bounding_box Bounding_box;
typedef CGAL::AABB_tree<Traits> Tree;
typedef Tree::Object_and_primitive_id Object_and_primitive_id;
typedef Tree::Primitive_id Primitive_id;

namespace CGAL {

template<typename AABBTraits,class Kernel,class Tag_ray_is_vertical=Tag_false>
class Ray_3_Triangle_3_traversal_traits
{
protected:
  //the status indicates whether the query point is strictly inside the polyhedron, and the number of intersected triangles if yes
  std::pair<boost::logic::tribool,std::size_t>& m_status;
  bool m_stop;
public:
  Ray_3_Triangle_3_traversal_traits(std::pair<boost::logic::tribool,std::size_t>& status)
    :m_status(status),m_stop(false)
  {m_status.first=true;}

  bool go_further() const { return !m_stop; }

  template<class Query>
  void intersection(const Query& query, const Primitive& primitive)
  {
    internal::r3t3_do_intersect_endpoint_position_visitor visitor;
    std::pair<bool,internal::R3T3_intersection::type> res=
      internal::do_intersect(primitive.datum(),query,Kernel(),visitor);
    
    if (res.first){
      switch (res.second){
        case internal::R3T3_intersection::CROSS_FACET:
          ++m_status.second;
        break;
        case internal::R3T3_intersection::ENDPOINT_IN_TRIANGLE:
          m_status.first=false;
          m_stop=true;
        break;
        default:
          m_status.first=boost::logic::indeterminate;
          m_stop=true;
      }
    }
  }
  
  template<class Query,class Node>
  bool do_intersect(const Query& query, const Node& node) const
  {
    return AABBTraits().do_intersect_object()(query, node.bbox());
  }
};


//specialization for vertical ray
template<typename AABBTraits,class Kernel>
class Ray_3_Triangle_3_traversal_traits<AABBTraits,Kernel,Tag_true>: 
  public Ray_3_Triangle_3_traversal_traits<AABBTraits,Kernel,Tag_false>
{
  typedef Ray_3_Triangle_3_traversal_traits<AABBTraits,Kernel,Tag_false> Base;
public:
  Ray_3_Triangle_3_traversal_traits(std::pair<boost::logic::tribool,std::size_t>& status):Base(status){}

  template <class Query>
  bool do_intersect(const Query& query, const Bbox_3& bbox) const
  {
    const Point& source=query.point(0);
    const Point& target=query.point(1);
    
    bool inc_z=target.z()>source.z();
    
    //the ray does not intersect the z-slab
    if ( ( inc_z && source.z()>bbox.zmax() )|| (!inc_z && source.z()<bbox.zmin()) ) return false;
    
    //the source is not in the x-slab
    if (source.x() > bbox.xmax() || source.x()<bbox.xmin()) return false;
    //check if the source is not in the y-slab
    return source.y() <= bbox.ymax() && source.y()>=bbox.ymin();
  }

  template <class Query,class Node>
  bool do_intersect(const Query& query, const Node& node) const
  {
    return do_intersect(query,node.bbox());
  }

private:
  typename Kernel::Point_2 x_project(const typename Kernel::Point_3& p) const{
    return typename Kernel::Point_2(p.y(),p.z());
  }
  typename Kernel::Point_2 y_project(const typename Kernel::Point_3& p) const{
    return typename Kernel::Point_2(p.x(),p.z());
  }
  typename Kernel::Point_2 z_project(const typename Kernel::Point_3& p) const{
    return typename Kernel::Point_2(p.x(),p.y());
  }
public:
  template<class Query>
  void intersection(const Query& query, const Primitive& primitive)
  {
    typename Kernel::Triangle_3 t=primitive.datum();
    if ( !do_intersect(query,t.bbox()) ) return;
    
    
    typename Kernel::Point_2 p0=z_project(t[0]);
    typename Kernel::Point_2 p1=z_project(t[1]);
    typename Kernel::Point_2 p2=z_project(t[2]);
    int indices[3]={0,1,2}; //to track whether triangle points have been swapt
    typename Kernel::Point_2 q=z_project( query.source() );
    
    Orientation orient_2=orientation(p0,p1,p2);
    
    //check whether the face has a normal vector in the xy-plane
    if (orient_2==COLLINEAR){
      //in that case the projection of the triangle along the z-axis is a segment.
      const typename Kernel::Point_2& other_point = p0!=p1?p1:p2;
      //~ if ( orientation(p0,other_point,q) != COLLINEAR ) return;///no intersection
      if ( orientation(p0,other_point,q) != COLLINEAR ) return;///no intersection
      
      //check if the ray source is above or below the triangle and compare it 
      //with the direction of the ray
      //TODO and if yes return
      //this is just an optimisation, the current code is valid
      
      this->m_status.first=boost::logic::indeterminate;
      this->m_stop=true;
      return;
    }
    
    
    //regular case
    if (orient_2==NEGATIVE){
      std::swap(p1,p2);
      std::swap(indices[1],indices[2]);
    }
    
    //check whether the ray intersect the supporting plane
    Orientation orient_3 = orientation(t[indices[0]],t[indices[1]],t[indices[2]],query.source());
    if ( orient_3!=COPLANAR && 
          (
            //indicates whether the ray is oriented toward the positive side of the plane
            ( POSITIVE == sign( query.to_vector().z() )  )
              ==
            //indicates whether the source of the ray is in the positive side of the plane
            (orient_3==POSITIVE)
          )
    ) return; //no intersection
    

    //position against first segment
    switch( orientation(p0,p1,q) ){
      case COLLINEAR:
        this->m_status.first=boost::logic::indeterminate;
        this->m_stop=true;
      case NEGATIVE:
        return;
      default:
      {}
    }
    //position against second segment
    switch( orientation(p1,p2,q) ){
      case COLLINEAR:
        this->m_status.first=boost::logic::indeterminate;
        this->m_stop=true;
      case NEGATIVE:
        return;
      default:
      {}
    }
    //position against third segment
    switch( orientation(p2,p0,q) ){
      case COLLINEAR:
        this->m_status.first=boost::logic::indeterminate;
        this->m_stop=true;
      case NEGATIVE:
        return;
      default:
      {}
    }

    if (orient_3==COPLANAR){
      //the endpoint is inside the triangle
      this->m_status.first=false;
      this->m_stop=true;
    }
    else
      ++(this->m_status.second);
  }
};
  
struct Inside {

  Polyhedron& polyhedron;
  Tree tree;
  int N;
  Point grid_base;
  std::vector<std::pair<Point,bool> > grid;
  double grid_dx, grid_dy, grid_dz;
  static const bool m_on_boundary_point_inside=false;
  static const bool m_use_a_vertical_ray=true;

  Inside(Polyhedron& polyhedron, int N = 0)
    : polyhedron(polyhedron), N(N)
  {
    tree.insert(polyhedron.facets_begin(),polyhedron.facets_end());
    tree.build();
    if(N>0){
      initialize_grid();
    }
  }

  void random_points(char* fname, int n)
  {
    std::cerr << "Write " << n << " points to file " << fname << std::endl;
    Random rg;
    Bounding_box bbox = tree.bbox();
    grid_dx = bbox.xmax() - bbox.xmin();
    grid_dy = bbox.ymax() - bbox.ymin();
    grid_dz = bbox.zmax() - bbox.zmin();
    
    std::ofstream out(fname);
    out.precision(16);
    for(int i=0; i < n; i++){
      out << bbox.xmin() + rg.get_double()* grid_dx << " " 
          << bbox.ymin() + rg.get_double()* grid_dy << " "
          << bbox.zmin() + rg.get_double()* grid_dz << std::endl;
    }
    out.close();
  }

	
  void initialize_grid()
  {
    Random rg;
    Bounding_box bbox = tree.bbox();
    grid_dx = bbox.xmax() - bbox.xmin();
    grid_dy = bbox.ymax() - bbox.ymin();
    grid_dz = bbox.zmax() - bbox.zmin();
 	
    bbox = Bounding_box(bbox.xmin()-0.01*grid_dx, bbox.ymin()-0.01*grid_dy, bbox.zmin()-0.01*grid_dz,
                        bbox.xmax()+0.01*grid_dx, bbox.ymax()+0.01*grid_dy, bbox.zmax()+0.01*grid_dz);
    grid_dx = (bbox.xmax() - bbox.xmin())/(N-1);
    grid_dy = (bbox.ymax() - bbox.ymin())/(N-1);
    grid_dz = (bbox.zmax() - bbox.zmin())/(N-1);
    grid_base = Point(bbox.xmin(), bbox.ymin(), bbox.zmin());
 

    grid.reserve(N*N*N);
    int points_inside = 0, points_outside=0;
    for(int i=0; i < N; i++){
      for(int j=0; j < N; j++){
        for(int k=0; k < N; k++){
          Point p(bbox.xmin()+i*grid_dx, bbox.ymin()+j*grid_dy, bbox.zmin()+k*grid_dz);
          if(i==0 || j==0 || k==0 || i==N-1 || j==N-1 || k==N-1){
            double eps = grid_dx/100;
            Vector v(rg.get_double(-eps,eps), rg.get_double(-eps,eps), rg.get_double(-eps,eps));
            p = p+ v;
            grid.push_back(std::make_pair(p,false));
              points_outside++;	
          } else {
            double eps = grid_dx/100;
            Vector v(rg.get_double(-eps,eps), rg.get_double(-eps,eps), rg.get_double(-eps,eps));
            p = p+ v;
            const Segment segment(p, grid.back().first);
            std::size_t M = (grid.back().second)? 0 : 1;
            bool inside = (tree.number_of_intersected_primitives(segment)&1) == M;
            if(inside){
              points_inside++;
            }else{
              points_outside++;
            }
            grid.push_back(std::make_pair(p,inside));
          }
        }
      }
    }
    std::cerr << points_inside << " points inside" << std::endl;
    std::cerr << points_outside << " points outside" << std::endl;
  }

private:
  template <class Query,bool ray_is_vertical>
  boost::logic::tribool 
  is_inside_ray_tree_traversal(const Query& query) const {
    std::pair<boost::logic::tribool,std::size_t> status(boost::logic::indeterminate,0);
    Ray_3_Triangle_3_traversal_traits<Traits,K,Boolean_tag<ray_is_vertical> > traversal_traits(status);
    tree.traversal(query, traversal_traits);
    if ( !boost::logic::indeterminate(status.first) ){
      if (status.first) return (status.second&1) == 1;
      //otherwise the point is on the facet
      return m_on_boundary_point_inside;
    }
    return boost::logic::indeterminate;
  }

public:

  bool operator()(const Point& p) const
  {
    const Bounding_box& bbox = tree.bbox();

    if(   p.x() < bbox.xmin() || p.x() > bbox.xmax()
       || p.y() < bbox.ymin() || p.y() > bbox.ymax()
       || p.z() < bbox.zmin() || p.z() > bbox.zmax() )
    {
      return false;
    }

    if(N>0)
    {
      Vector v = p - grid_base;
      int i = boost::math::round(v.x() / grid_dx);  
      int j =  boost::math::round(v.y() / grid_dy);  
      int k =  boost::math::round(v.z() / grid_dz);

      if(i>N-1)i=N-1;
      if(j>N-1)j=N-1;
      if(k>N-1)k=N-1;
      int index = i*N*N + j*N + k;
       
      const std::pair<Point,bool>& close_point = grid[index];
      K::Construct_segment_3 segment = K().construct_segment_3_object();
      const Segment query = segment(p, close_point.first);
      if(p == close_point.first){
        std::cerr << "error"  << std::endl;
      }
      std::size_t M = (close_point.second)? 0 : 1; 
      bool res = (tree.number_of_intersected_primitives(query)&1) == M;
      return res;
    } 
    else 
    {
      K::Construct_ray_3 make_ray = K().construct_ray_3_object();
      K::Construct_vector_3 make_vector = K().construct_vector_3_object();
     
      //start with a vertical ray
      //~ Ray query = ray(p, vector(CGAL::ORIGIN,*random_point));
      
      Random_points_on_sphere_3<Point> random_point(1.);
      //the direction of the vertical ray depends on the position of the point in the bbox
      //in order to limit the expected number of nodes visited.
      Ray query = 
        m_use_a_vertical_ray ?
        make_ray(p, make_vector(0,0,(2*p.z() <  tree.bbox().zmax()+tree.bbox().zmin()?-1:1))) :
        make_ray(p, make_vector(CGAL::ORIGIN,*random_point));
      boost::logic::tribool res=is_inside_ray_tree_traversal<Ray,true>(query);
      while (boost::logic::indeterminate(res)){
        //retry with a random ray
        query = make_ray(p, make_vector(CGAL::ORIGIN,*random_point++));
        res=is_inside_ray_tree_traversal<Ray,false>(query);
      }
      return res;
    }
  }
  
  //the original version
  bool operator()(const Point& p,bool) const
  {
    K::Construct_ray_3 ray = K().construct_ray_3_object();
    K::Construct_vector_3 vector = K().construct_vector_3_object();
   
    Random_points_on_sphere_3<Point> random_point(1.);
    
    //const Ray query = ray(p, vector(CGAL::ORIGIN,*random_point));
    
    //the direction of the vertical ray depends on the position of the point in the bbox
    //in order to limit the expected number of nodes visited.
    const Ray query = ray(p, vector(0,0, (2*p.z() <  tree.bbox().zmax()+tree.bbox().zmin()?-1:1) ));

    return (tree.number_of_intersected_primitives(query)&1) == 1;
  }
  
};

} // namespace CGAL


int main(int argc, char* argv[])
{
  std::cerr.precision(17);

  std::ifstream points_file(argv[2]);

  std::vector<Point> points;
  std::copy(std::istream_iterator<Point>(points_file),
            std::istream_iterator<Point>(),
            std::back_inserter(points));


  int gridsize = 10;
  if(argc>3){
    gridsize = boost::lexical_cast<int>(argv[3]);
  }
  std::cerr << "gridsize = " << gridsize << std::endl;
  
  int nb_points=points.size();
  
  std::vector<bool> ray_res(nb_points);
  std::vector<bool> grid_res(nb_points);
  
  //using ray
  {
    Polyhedron polyhedron;
    std::ifstream polyhedron_file(argv[1]);
    polyhedron_file >> polyhedron;
    std::cerr << "|V| = " << polyhedron.size_of_vertices() << std::endl;
    
    CGAL::Timer timer;
    timer.start();
    CGAL::Inside inside_with_ray(polyhedron,0);
    timer.stop();
    std::cerr <<"Using ray"<< std::endl;
    std::cerr << "  Preprocessing took " << timer.time() << " sec." << std::endl;
    timer.reset();  
    int n_inside = 0;
    
    
    timer.start();
    for(int k=0;k<nb_points;++k){
      ray_res[k]=inside_with_ray(points[k]);
      if(ray_res[k]){
        ++n_inside;
      }
    }
    timer.stop();
    std::cerr << "  " << n_inside << " points inside " << std::endl;
    std::cerr << "  " << points.size() - n_inside << " points outside "  << std::endl;
    std::cerr << " Queries took " << timer.time() << " sec." << std::endl; 

  }
  
  //using grid
  {
    Polyhedron polyhedron;
    std::ifstream polyhedron_file(argv[1]);
    polyhedron_file >> polyhedron;
    std::cerr << "|V| = " << polyhedron.size_of_vertices() << std::endl;
    
    CGAL::Timer timer;
    timer.start();
    CGAL::Inside inside_with_grid(polyhedron, gridsize);
    timer.stop();
    std::cerr <<"Using grid"<< std::endl;
    std::cerr << "  Preprocessing took " << timer.time() << " sec." << std::endl;
    timer.reset();

    if(argc>5){
      inside_with_grid.random_points(argv[4], boost::lexical_cast<int>(argv[5]) );
    }
    
    int n_inside = 0;
    timer.start();
    for(int k=0;k<nb_points;++k){
      grid_res[k]=inside_with_grid(points[k]);
      if(grid_res[k]){
        ++n_inside;
      }
    }
    timer.stop();
    std::cerr << "  " << n_inside << " points inside " << std::endl;
    std::cerr << "  " << points.size() - n_inside << " points outside "  << std::endl;
    std::cerr << "  Queries took " << timer.time() << " sec." << std::endl; 
  }

  for(int k=0;k<nb_points;++k){
    if(ray_res[k]!=grid_res[k]){
      std::cerr << "WARNING: Result is different for point " << k << std::endl;
    }
  }
  
  //using original code
  {
    Polyhedron polyhedron;
    std::ifstream polyhedron_file(argv[1]);
    polyhedron_file >> polyhedron;
    std::cerr << "|V| = " << polyhedron.size_of_vertices() << std::endl;

    
    std::cerr <<"Using ray (original code)"<< std::endl;
    CGAL::Inside inside_with_ray(polyhedron,0);
    
    CGAL::Timer timer;  
    int n_inside = 0;
    timer.start();
    for(int k=0;k<nb_points;++k)
      if(inside_with_ray(points[k],true)) ++n_inside;
    timer.stop();
    std::cerr << "  " << n_inside << " points inside " << std::endl;
    std::cerr << "  " << points.size() - n_inside << " points outside "  << std::endl;
    std::cerr << "  Queries took " << timer.time() << " sec." << std::endl; 
  }
  
  return 0;
}
