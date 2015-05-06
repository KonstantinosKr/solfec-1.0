#include <cmath>

#include <assert.h> 
#include <algorithm>
#include <vector>
#include <set>
#include <list>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <string.h>
#include <limits.h>

extern "C"
{
#include "../ext/tetgen/tetsol.h"
#include "../sol.h"
#include "../dom.h"
#include "../msh.h"
#include "../dio.h"
#include "../fem.h"
#include "../fra.h"
#include "../mem.h"
#include "../alg.h"
#include "../fem.h"
#include "../pck.h"
#include "../lap.h"
#include "../kdt.h"
}

#include "costy.h"

#define PI 3.14159265358979323846

// calculated integral on triangle
void integTri3d(MESH* _msh, FACE* _fac, double* _q, Vector_3& _integ) {
	
	Vector_3 pt[3];

	pt[0].x = _msh->ref_nodes[ _fac->nodes[0] ][0] + _q[ 3*_fac->nodes[0] ];
	pt[0].y = _msh->ref_nodes[ _fac->nodes[0] ][1] + _q[ 3*_fac->nodes[0] + 1];
	pt[0].z = _msh->ref_nodes[ _fac->nodes[0] ][2] + _q[ 3*_fac->nodes[0] + 2];

	pt[1].x = _msh->ref_nodes[ _fac->nodes[1] ][0] + _q[ 3*_fac->nodes[1] ];
	pt[1].y = _msh->ref_nodes[ _fac->nodes[1] ][1] + _q[ 3*_fac->nodes[1] + 1];
	pt[1].z = _msh->ref_nodes[ _fac->nodes[1] ][2] + _q[ 3*_fac->nodes[1] + 2];

	pt[2].x = _msh->ref_nodes[ _fac->nodes[2] ][0] + _q[ 3*_fac->nodes[2] ];
	pt[2].y = _msh->ref_nodes[ _fac->nodes[2] ][1] + _q[ 3*_fac->nodes[2] + 1];
	pt[2].z = _msh->ref_nodes[ _fac->nodes[2] ][2] + _q[ 3*_fac->nodes[2] + 2];

	// get area
	Vector_3 V(pt[1].x-pt[0].x, pt[1].y-pt[0].y, pt[1].z-pt[0].z);
	Vector_3 W(pt[2].x-pt[0].x, pt[2].y-pt[0].y, pt[2].z-pt[0].z);

	Vector_3 normal;
	cross(V, W, normal);
	double area = 0.5 * norm(normal);

	// calculate integral over triangle
	_integ.x = (1.0/3.0) * (pt[0].x + pt[1].x + pt[2].x) * area;
	_integ.y = (1.0/3.0) * (pt[0].y + pt[1].y + pt[2].y) * area;
	_integ.z = (1.0/3.0) * (pt[0].z + pt[1].z + pt[2].z) * area;

}

//_____________________________________________________________________________________________________________________________________

// obtain average data contained in a Vector_3 object vector
void average(std::vector<Vector_3>& vec, Vector_3& avg) {

	for (std::vector<Vector_3>::iterator it = vec.begin(); it != vec.end(); ++it)
	{
		avg.x += (*it).x;
		avg.y += (*it).y;
		avg.z += (*it).z;
	}

	avg.x /= vec.size();
	avg.y /= vec.size();
	avg.z /= vec.size();

}


// subtract value from Vector_3 vector
void subtract(std::vector<Vector_3>& vec, const Vector_3& val) {

	for (std::vector<Vector_3>::iterator it = vec.begin(); it != vec.end(); ++it)
	{
		(*it).x -= val.x;
		(*it).y -= val.y;
		(*it).z -= val.z;
	}

}


// remove rigid body translation
void rm_rbt(std::vector<Vector_3>& vec_3) {

	Vector_3 avg_val;
	average(vec_3, avg_val);
	subtract(vec_3, avg_val);

	// std::cout << "--> Rigid body translation mitigated\n";

}

//_____________________________________________________________________________________________________________________________________

// output VTK file: linear tetrahedrals
void output_VTK(MESH *msh, std::set<int>& uqVertsIndx, std::vector<Vector_3>& disp, char *output) {

  ELEMENT* ele;

  int i;
  int elno = msh->surfeles_count + msh->bulkeles_count; // element number

  // output mesh & displacement data
  std::ofstream fin;
  fin.open(output);
    
  // print out the header data
  fin << "# vtk DataFile Version 2.0\n";
  fin << "Mesh Output\n";
  fin << "ASCII\n";

  //print out the coordinates of the vertices
  fin << "DATASET UNSTRUCTURED_GRID\n";
  fin << "POINTS " << msh->nodes_count << " float\n";

  // print out point coordinates
  for (i = 0; i < msh->nodes_count; i++) 
  	fin << msh->ref_nodes[i][0] << " " << msh->ref_nodes[i][1] << " " << msh->ref_nodes[i][2] << std::endl;

  // print out elements
  fin << std::endl;
  fin << "CELLS " << elno << " " << 5*elno << std::endl; //(4+1)*no_of_tet

  for (ele = msh->surfeles; ele; ele = ele->next)
  	fin << ele->nodes[0] << " " << ele->nodes[1] << " " << ele->nodes[2] << " " << ele->nodes[3] << std::endl;

  for (ele = msh->bulkeles; ele; ele = ele->next)
  	fin << ele->nodes[0] << " " << ele->nodes[1] << " " << ele->nodes[2] << " " << ele->nodes[3] << std::endl;

  // print out element type
  fin << std::endl;
  fin << "CELL_TYPES " << elno << std::endl;
  for (i = 0; i < elno; ++i) 
  	fin << "10" << std::endl;

  // print out point data
  fin << std::endl;
  fin << "POINT_DATA " << msh->nodes_count << std::endl;
  fin << "Vectors displacement float\n";
  //fin << "LOOKUP_TABLE default\n";
  for (i = 0; i < msh->nodes_count; ++i)
  {
  	if (uqVertsIndx.count(i) != 0) fin << disp[i].x << " " << disp[i].y << " " << disp[i].z << std::endl;
  	else fin << "0 0 0\n";
  }

  fin.close();

  //std::cout << "--> Mesh successfully output to VTK file format\n";

}

//_____________________________________________________________________________________________________________________________________

// map displacement and pressure data on generated tetrahedral mesh in reference configuration + calc requisite data for optimisation
void mapping(BODY* bod, MESH* msh, FS* it, std::vector<Vector_3>& disp, std::vector<Vector_3>& nl, std::vector<Vector_3>& ig, std::set<int>& uqVerts) {

	int i; //fano;
	double extents[6], *q, *u;
	FS *jt;
	FACE *fac;
	ELEMENT *ele;
	KDT *kd;

	std::set<int> faceVertsIndx;
	Vector_3 integ;

	//______________________________________________________
	// allocate displacements on the tet mesh
  if (!(q = (double*)malloc (6 * msh->nodes_count * sizeof (double))))
  {
    fprintf (stderr, "ERROR: out of memory");
    exit (1);
  }
  u = q + 3 * msh->nodes_count;

  // map displacements to the generated mesh
  FEM_Map_State ((MESH*)bod->shape->data, bod->conf, bod->velo, msh, q, u); // only bod->disp to q mapping is used 

  // loop through faces and insert vertices into set
  for (fac = msh->faces; fac; fac = fac->n)
  {
  	faceVertsIndx.insert( fac->nodes[0] );
  	faceVertsIndx.insert( fac->nodes[1] );
  	faceVertsIndx.insert( fac->nodes[2] );
  }
  //______________________________________________________
  // allocate pressure on surface of tet mesh
  //ERRMEM ( p = malloc (fano * sizeof (double [3])) );

  // map faces to a kd-tree for quick point queries
  kd = KDT_Create (msh->nodes_count, (double*)msh->ref_nodes, 0.0);
  
  for (ele = msh->surfeles; ele; ele = ele->next)
  { 
    ELEMENT_Ref_Extents (msh, ele, extents);
    for (fac = ele->faces; fac; fac = fac->next)
    	KDT_Drop (kd, extents, fac);
  }

  // for (i = 0; i < fano; i++)
  //	SET (p[i], 0.0); // zero face pressure

	// for each point force in this instance
	for (jt = it; jt; jt = jt->inext)
	{
		double (*ref)[3] = msh->ref_nodes;
		double a[3], b[3], c[3], area;
		SET *set = NULL, *item;
		double *qa, *qb, *qc;

		// set up search extents 
		extents[0] = jt->point[0] - jt->radius - GEOMETRIC_EPSILON; 
		extents[1] = jt->point[1] - jt->radius - GEOMETRIC_EPSILON;
		extents[2] = jt->point[2] - jt->radius - GEOMETRIC_EPSILON;
		extents[3] = jt->point[0] + jt->radius + GEOMETRIC_EPSILON;
		extents[4] = jt->point[1] + jt->radius + GEOMETRIC_EPSILON;
		extents[5] = jt->point[2] + jt->radius + GEOMETRIC_EPSILON;

		KDT_Pick_Extents (kd, extents, &set); // pick kd-tree leaves within given extents

		for (item = SET_First (set); item; item = SET_Next (item))
		{
			KDT *leaf = (KDT*)item->data;

			for (i = 0; i < leaf->n; i++)
			{
				fac = (FACE*)leaf->data[i]; // face dropped into this leaf

				qa = &q[3*fac->nodes[0]];
				qb = &q[3*fac->nodes[1]];
				qc = &q[3*fac->nodes[2]];

				// current face node coordinates
				ADD (ref[fac->nodes[0]], qa, a);
				ADD (ref[fac->nodes[1]], qb, b);
				ADD (ref[fac->nodes[2]], qc, c);

				TRIANGLE_AREA (a, b, c, area); // current face area
				assert(area > 0.0);

				// get unique indices of the nodes
				uqVerts.insert( fac->nodes[0] );
				uqVerts.insert( fac->nodes[1] );
				uqVerts.insert( fac->nodes[2] );

				// insert normals and integ into std::vector< Vector_3 >
				integTri3d(msh, fac, q, integ);

				nl.push_back( Vector_3(fac->normal[0], fac->normal[1], fac->normal[2]) );
				ig.push_back( integ );

				integ.zero();
			}
		}
    
		SET_Free (NULL, &set);
	}
	//______________________________________________________
	// allocate node displacements on the surface to std::vector< Vector_3 >
	for (i = 0; i < msh->nodes_count; i++)
	{
		if (faceVertsIndx.count(i) != 0) disp.push_back( Vector_3(q[3*i], q[3*i+1], q[3*i+2]) );
		else disp.push_back( Vector_3(0,0,0) );
	}
	//______________________________________________________

}

//_____________________________________________________________________________________________________________________________________

extern "C"
{
// main routine to remove rigid body motion from displacement data
int rbmm_main(BODY* _bod, double volume, double quality, char* _output) {

	SOLFEC *sol = _bod->dom->solfec;
	MESH* _mesh;
	FS *list, *lit;
	std::vector<Vector_3> vertsDisp, normals, integrals;
	std::set<int> uniqueVerticesIndx;

	if (!(_bod->flags & BODY_CHECK_FRACTURE) || sol->mode == SOLFEC_WRITE) return 0;

	list = fracture_state_read (_bod);

	if (list)
	{
		MESH *copy = MESH_Copy ((MESH*)_bod->shape->data);
		MESH_Update (copy, NULL, NULL, NULL); // reference configuration 
		_mesh = tetrahedralize1 (copy, volume, quality, -INT_MAX, -INT_MAX, 0, 180, -1); // generate tet mesh in reference configuration
		MESH_Destroy (copy);
		
		// go to the last element of the list, which corresponds
		// to the first (in time) instance of fracture
		for (lit = list; lit->next; lit = lit->next) {}

		// map and calculate rbbm data
		mapping(_bod, _mesh, lit, vertsDisp, normals, integrals, uniqueVerticesIndx);

		// remove rigid body translation
		rm_rbt(vertsDisp);

		// output VTK format
		output_VTK(_mesh, uniqueVerticesIndx, vertsDisp, _output);

		fracture_state_free (list);
		MESH_Destroy (_mesh);

		return 1;
	}

	return 0;

}
}
