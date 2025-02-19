/* Copyright (c) <2003-2019> <Julio Jerez, Newton Game Dynamics>
* 
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
* 
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
* 
* 1. The origin of this software must not be misrepresented; you must not
* claim that you wrote the original software. If you use this software
* in a product, an acknowledgment in the product documentation would be
* appreciated but is not required.
* 
* 2. Altered source versions must be plainly marked as such, and must not be
* misrepresented as being the original software.
* 
* 3. This notice may not be removed or altered from any source distribution.
*/

#include "dgPhysicsStdafx.h"
#include "dgBody.h"
#include "dgWorld.h"
#include "dgContact.h"
#include "dgCollisionInstance.h"
#include "dgWorldDynamicUpdate.h"

#define REST_RELATIVE_VELOCITY			dgFloat32 (1.0e-3f)
#define MAX_DYNAMIC_FRICTION_SPEED		dgFloat32 (0.3f)
#define MAX_PENETRATION_STIFFNESS		dgFloat32 (50.0f)
//#define MAX_SEPARATING_SPEED			dgFloat32 (4.0f)


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
dgContactMaterial::dgContactMaterial()
	:m_dir0 (dgFloat32 (0.0f))
	,m_dir1 (dgFloat32 (0.0f))
	,m_userData(NULL)
	,m_aabbOverlap(NULL)
	,m_processContactPoint(NULL)
	,m_contactGeneration(NULL)
	,m_compoundAABBOverlap(NULL)
{
	//	dgAssert ( dgInt32 (sizeof (dgContactMaterial) & 15) == 0);
	dgAssert ((((dgUnsigned64) this) & 15) == 0);
	m_point = dgVector (dgFloat32 (0.0f));
	m_softness = dgFloat32 (0.1f);
	m_restitution = dgFloat32 (0.4f);

	m_staticFriction0 = dgFloat32 (0.9f);
	m_staticFriction1 = dgFloat32 (0.9f);
	m_dynamicFriction0 = dgFloat32 (0.5f);
	m_dynamicFriction1 = dgFloat32 (0.5f);
	m_dir0_Force.m_force = dgFloat32 (0.0f);
	m_dir0_Force.m_impact = dgFloat32 (0.0f);
	m_dir1_Force.m_force = dgFloat32 (0.0f);
	m_dir1_Force.m_impact = dgFloat32 (0.0f);
	m_normal_Force.m_force = dgFloat32 (0.0f);
	m_normal_Force.m_impact = dgFloat32 (0.0f);
	m_skinThickness = dgFloat32 (0.0f);
	m_flags = m_collisionEnable | m_friction0Enable | m_friction1Enable;
}

dgContact::dgContact(dgWorld* const world, const dgContactMaterial* const material, dgBody* const body0, dgBody* const body1)
	:dgConstraint()
	,dgList<dgContactMaterial>(world->GetAllocator())
	,m_positAcc(dgFloat32 (10.0f))
	,m_rotationAcc ()
	,m_material(material)
	,m_closestDistance (dgFloat32 (0.0f))
	,m_separationDistance(dgFloat32 (0.0f))
	,m_timeOfImpact(dgFloat32 (1.0e10f))
	,m_impulseSpeed (dgFloat32 (0.0f))
	,m_contactPruningTolereance(world->GetContactMergeTolerance())
	,m_broadphaseLru(0)
	,m_killContact(0)
	,m_isNewContact(1)
	,m_skeletonIntraCollision(1)
	,m_skeletonSelftCollision(1)
{
	dgAssert ((((dgUnsigned64) this) & 15) == 0);
	m_maxDOF = 0;
	m_isActive = 0;
	m_enableCollision = true;
	m_constId = m_contactConstraint;

	if (body0->m_invMass.m_w > dgFloat32(0.0f)) {
		m_body0 = body0;
		m_body1 = body1;
	} else {
		m_body0 = body1;
		m_body1 = body0;
	}

	if (world->m_onCreateContact) {
		world->m_onCreateContact(world, this);
	}
}

dgContact::dgContact(dgContact* const clone)
	:dgConstraint(*clone)
	,dgList<dgContactMaterial>(clone->GetAllocator())
	,m_positAcc(clone->m_positAcc)
	,m_rotationAcc(clone->m_rotationAcc)
	,m_separtingVector (clone->m_separtingVector)
	,m_material(clone->m_material)
	,m_closestDistance(clone->m_closestDistance)
	,m_separationDistance(clone->m_separationDistance)
	,m_timeOfImpact(clone->m_timeOfImpact)
	,m_impulseSpeed (clone->m_impulseSpeed)
	,m_contactPruningTolereance(clone->m_contactPruningTolereance)
	,m_broadphaseLru(clone->m_broadphaseLru)
	,m_killContact(clone->m_killContact)
	,m_isNewContact(clone->m_isNewContact)
	,m_skeletonIntraCollision(clone->m_skeletonIntraCollision)
	,m_skeletonSelftCollision(clone->m_skeletonSelftCollision)
{
	dgAssert((((dgUnsigned64) this) & 15) == 0);
	m_body0 = clone->m_body0;
	m_body1 = clone->m_body1;
	m_maxDOF = clone->m_maxDOF;
	m_constId = m_contactConstraint;
	m_isActive = clone->m_isActive;
	m_enableCollision = clone->m_enableCollision;
	Merge (*clone);

	if (m_body0->m_world->m_onCreateContact) {
		dgAssert(clone->m_body0);
		m_body0->m_world->m_onCreateContact(clone->m_body0->m_world, this);
	}
}

dgContact::~dgContact()
{
	dgAssert(m_body0);
	if (m_body0->m_world && m_body0->m_world->m_onDestroyContact) {
		m_body0->m_world->m_onDestroyContact(m_body0->m_world, this);
	}

	dgList<dgContactMaterial>::RemoveAll();
}


void dgContact::SwapBodies()
{
	dgSwap (m_body0, m_body1);
	dgSwap (m_link0, m_link1);
}

void dgContact::GetInfo (dgConstraintInfo* const info) const
{
	memset (info, 0, sizeof (dgConstraintInfo));
	InitInfo (info);
	info->m_collideCollisionOn = GetCount();
	strcpy (info->m_discriptionType, "contact");
}

void dgContact::CalculatePointDerivative (dgInt32 index, dgContraintDescritor& desc, const dgVector& dir, const dgPointParam& param) const
{
	dgAssert (m_body0);
	dgAssert (m_body1);

	dgJacobian &jacobian0 = desc.m_jacobian[index].m_jacobianM0; 
	dgJacobian &jacobian1 = desc.m_jacobian[index].m_jacobianM1;
	jacobian0.m_linear = dir;
	jacobian1.m_linear = dir * dgVector::m_negOne;

	jacobian0.m_angular = param.m_r0.CrossProduct(dir);
	jacobian1.m_angular = dir.CrossProduct(param.m_r1);

	dgAssert(jacobian0.m_linear.m_w == dgFloat32(0.0f));
	dgAssert(jacobian0.m_angular.m_w == dgFloat32(0.0f));
	dgAssert(jacobian1.m_linear.m_w == dgFloat32(0.0f));
	dgAssert(jacobian1.m_angular.m_w == dgFloat32(0.0f));
}

dgUnsigned32 dgContact::JacobianDerivative (dgContraintDescritor& params)
{
	dgInt32 frictionIndex = 0;
	m_impulseSpeed = dgFloat32 (0.0f);
	if (m_maxDOF) {
		dgInt32 i = 0;
		frictionIndex = GetCount();
		for (dgList<dgContactMaterial>::dgListNode* node = GetFirst(); node; node = node->GetNext()) {
			const dgContactMaterial& contact = node->GetInfo(); 
			JacobianContactDerivative (params, contact, i, frictionIndex);
			i ++;
		}
	}

	return dgUnsigned32 (frictionIndex);
}

void dgContact::JacobianContactDerivative (dgContraintDescritor& params, const dgContactMaterial& contact, dgInt32 normalIndex, dgInt32& frictionIndex) 
{
	dgPointParam pointData;

	dgFloat32 impulseOrForceScale = (params.m_timestep > dgFloat32 (0.0f)) ? params.m_invTimestep : dgFloat32 (1.0f);

	InitPointParam (pointData, dgFloat32 (1.0f), contact.m_point, contact.m_point);
	CalculatePointDerivative (normalIndex, params, contact.m_normal, pointData); 

	const dgVector veloc0 = m_body0->m_veloc;
	const dgVector omega0 = m_body0->m_omega;
	const dgVector veloc1 = m_body1->m_veloc;
	const dgVector omega1 = m_body1->m_omega;

	const dgVector gyroAlpha0(m_body0->m_gyroAlpha);
	const dgVector gyroAlpha1(m_body1->m_gyroAlpha);

	dgAssert (contact.m_normal.m_w == dgFloat32 (0.0f));
	const dgJacobian &normalJacobian0 = params.m_jacobian[normalIndex].m_jacobianM0;
	const dgJacobian &normalJacobian1 = params.m_jacobian[normalIndex].m_jacobianM1;

	dgFloat32 restitutionCoefficient = contact.m_restitution;
	dgFloat32 relVeloc = -(normalJacobian0.m_linear * veloc0 + normalJacobian0.m_angular * omega0 + normalJacobian1.m_linear * veloc1 + normalJacobian1.m_angular * omega1).AddHorizontal().GetScalar();
	dgFloat32 penetration = dgClamp (contact.m_penetration - DG_RESTING_CONTACT_PENETRATION, dgFloat32(0.0f), dgFloat32(0.5f));
//restitutionCoefficient = 0.0f;
//penetration = 0.0f;

	dgFloat32 penetrationStiffness = MAX_PENETRATION_STIFFNESS * contact.m_softness;
	dgFloat32 penetrationVeloc = penetration * penetrationStiffness;
	dgAssert (dgAbs (penetrationVeloc - MAX_PENETRATION_STIFFNESS * contact.m_softness * penetration) < dgFloat32 (1.0e-6f));
	//if (relVelocErr > REST_RELATIVE_VELOCITY) {
	//	relVelocErr *= (restitutionCoefficient + dgFloat32 (1.0f));
	//}
	dgFloat32 restitutionVelocity = (relVeloc > REST_RELATIVE_VELOCITY) ? relVeloc * restitutionCoefficient : dgFloat32 (0.0f);
	m_impulseSpeed = dgMax (m_impulseSpeed, restitutionVelocity);

	params.m_penetration[normalIndex] = penetration;
	params.m_restitution[normalIndex] = restitutionCoefficient;
	params.m_penetrationStiffness[normalIndex] = penetrationStiffness;
	params.m_forceBounds[normalIndex].m_low = dgFloat32 (0.0f);
	params.m_forceBounds[normalIndex].m_normalIndex = DG_INDEPENDENT_ROW;
	params.m_forceBounds[normalIndex].m_jointForce = (dgForceImpactPair*) &contact.m_normal_Force;
	params.m_jointStiffness[normalIndex] = dgFloat32 (0.0f);

	const dgFloat32 relGyro = (normalJacobian0.m_angular * m_body0->m_gyroAlpha + normalJacobian1.m_angular * m_body1->m_gyroAlpha).AddHorizontal().GetScalar();
	//params.m_jointAccel[normalIndex] = relGyro + (relVelocErr + penetrationVeloc) * impulseOrForceScale;
	relVeloc += dgMax (restitutionVelocity, penetrationVeloc);
	params.m_jointAccel[normalIndex] = relGyro + relVeloc * impulseOrForceScale;
	if (contact.m_flags & dgContactMaterial::m_overrideNormalAccel) {
		params.m_jointAccel[normalIndex] += contact.m_normal_Force.m_force;
	}

//return;
	// first dir friction force
	if (contact.m_flags & dgContactMaterial::m_friction0Enable) {
		dgInt32 jacobIndex = frictionIndex;
		frictionIndex += 1;
		dgAssert (contact.m_dir0.m_w == dgFloat32 (0.0f));
		CalculatePointDerivative (jacobIndex, params, contact.m_dir0, pointData); 

		const dgJacobian &jacobian0 = params.m_jacobian[jacobIndex].m_jacobianM0;
		const dgJacobian &jacobian1 = params.m_jacobian[jacobIndex].m_jacobianM1;
		dgFloat32 relVelocErr = -(jacobian0.m_linear * veloc0 + jacobian0.m_angular * omega0 + jacobian1.m_linear * veloc1 + jacobian1.m_angular * omega1).AddHorizontal().GetScalar();

		params.m_forceBounds[jacobIndex].m_normalIndex = dgInt16 ((contact.m_flags & dgContactMaterial::m_override0Friction) ? DG_INDEPENDENT_ROW : normalIndex);
		params.m_jointStiffness[jacobIndex] = dgFloat32 (0.0f);

		params.m_restitution[jacobIndex] = dgFloat32(0.0f);
		params.m_penetration[jacobIndex] = dgFloat32(0.0f);

		params.m_penetrationStiffness[jacobIndex] = dgFloat32 (0.0f);
		if (contact.m_flags & dgContactMaterial::m_override0Accel) {
			// note: using restitution been negative to indicate that the acceleration was override
			//dgAssert(dgAbs(relVelocErr * impulseOrForceScale - contact.m_dir0_Force.m_force) < 1.0e-3f);
			params.m_restitution[jacobIndex] = dgFloat32 (-1.0f);
			params.m_jointAccel[jacobIndex] = contact.m_dir0_Force.m_force;
		} else {
			const dgFloat32 relFrictionGyro = (jacobian0.m_angular * gyroAlpha0 + jacobian1.m_angular * gyroAlpha1).AddHorizontal().GetScalar();
			params.m_restitution[jacobIndex] = dgFloat32 (0.0f);
			params.m_jointAccel[jacobIndex] = relFrictionGyro + relVelocErr * impulseOrForceScale;
		}
		if (dgAbs (relVelocErr) > MAX_DYNAMIC_FRICTION_SPEED) {
			params.m_forceBounds[jacobIndex].m_low = -contact.m_dynamicFriction0;
			params.m_forceBounds[jacobIndex].m_upper = contact.m_dynamicFriction0;
		} else {
			params.m_forceBounds[jacobIndex].m_low = -contact.m_staticFriction0;
			params.m_forceBounds[jacobIndex].m_upper = contact.m_staticFriction0;
		}
		params.m_forceBounds[jacobIndex].m_jointForce = (dgForceImpactPair*)&contact.m_dir0_Force;
	}

	if (contact.m_flags & dgContactMaterial::m_friction1Enable) {
		dgInt32 jacobIndex = frictionIndex;
		frictionIndex += 1;
		dgAssert (contact.m_dir1.m_w == dgFloat32 (0.0f));
		CalculatePointDerivative (jacobIndex, params, contact.m_dir1, pointData); 

		const dgJacobian &jacobian0 = params.m_jacobian[jacobIndex].m_jacobianM0;
		const dgJacobian &jacobian1 = params.m_jacobian[jacobIndex].m_jacobianM1;
		dgFloat32 relVelocErr = -(jacobian0.m_linear * veloc0 + jacobian0.m_angular * omega0 + jacobian1.m_linear * veloc1 + jacobian1.m_angular * omega1).AddHorizontal().GetScalar();

		params.m_forceBounds[jacobIndex].m_normalIndex = dgInt16 ((contact.m_flags & dgContactMaterial::m_override1Friction) ? DG_INDEPENDENT_ROW : normalIndex);
		params.m_jointStiffness[jacobIndex] = dgFloat32(0.0f);
		
		params.m_restitution[jacobIndex] = dgFloat32 (0.0f);
		params.m_penetration[jacobIndex] = dgFloat32 (0.0f);
		params.m_penetrationStiffness[jacobIndex] = dgFloat32 (0.0f);
		if (contact.m_flags & dgContactMaterial::m_override1Accel) {
			// note: using restitution been negative to indicate that the acceleration was override
			//dgAssert(dgAbs(relVelocErr * impulseOrForceScale - contact.m_dir1_Force.m_force) < 1.0e-3f);
			params.m_restitution[jacobIndex] = dgFloat32 (-1.0f);
			params.m_jointAccel[jacobIndex] = contact.m_dir1_Force.m_force;
		} else {
			const dgFloat32 relFrictionGyro = (jacobian0.m_angular * gyroAlpha0 + jacobian1.m_angular * gyroAlpha1).AddHorizontal().GetScalar();
			params.m_restitution[jacobIndex] = dgFloat32 (0.0f);
			params.m_jointAccel[jacobIndex] = relFrictionGyro + relVelocErr * impulseOrForceScale;
		}
		if (dgAbs (relVelocErr) > MAX_DYNAMIC_FRICTION_SPEED) {
			params.m_forceBounds[jacobIndex].m_low = - contact.m_dynamicFriction1;
			params.m_forceBounds[jacobIndex].m_upper = contact.m_dynamicFriction1;
		} else {
			params.m_forceBounds[jacobIndex].m_low = - contact.m_staticFriction1;
			params.m_forceBounds[jacobIndex].m_upper = contact.m_staticFriction1;
		}
		params.m_forceBounds[jacobIndex].m_jointForce = (dgForceImpactPair*)&contact.m_dir1_Force;
	}
}

void dgContact::JointAccelerations(dgJointAccelerationDecriptor* const params)
{
	const dgVector& bodyVeloc0 = m_body0->m_veloc;
	const dgVector& bodyOmega0 = m_body0->m_omega;
	const dgVector& bodyVeloc1 = m_body1->m_veloc;
	const dgVector& bodyOmega1 = m_body1->m_omega;

	const dgInt32 count = params->m_rowsCount;

	dgFloat32 timestep = dgFloat32 (1.0f);
	dgFloat32 invTimestep = dgFloat32 (1.0f);
	if (params->m_timeStep > dgFloat32 (0.0f)) {
		timestep = params->m_timeStep;
		invTimestep = params->m_invTimeStep;
	}

	dgRightHandSide* const rightHandSide = params->m_rightHandSide;
	const dgLeftHandSide* const leftHandSide = params->m_leftHandSide;

	const dgVector gyroAlpha0(m_body0->m_gyroAlpha);
	const dgVector gyroAlpha1(m_body1->m_gyroAlpha);

	for (dgInt32 k = 0; k < count; k ++) {
		// note: using restitution been negative to indicate that the acceleration was override
		if (rightHandSide[k].m_restitution >= dgFloat32 (0.0f)) {
			dgRightHandSide* const rhs = &rightHandSide[k];
			const dgLeftHandSide* const row = &leftHandSide[k];
			const dgJacobian &jacobian0 = row->m_Jt.m_jacobianM0;
			const dgJacobian &jacobian1 = row->m_Jt.m_jacobianM1;

			dgVector relVeloc (jacobian0.m_linear * bodyVeloc0 + jacobian0.m_angular * bodyOmega0 + jacobian1.m_linear * bodyVeloc1 + jacobian1.m_angular * bodyOmega1);
			dgFloat32 vRel = relVeloc.AddHorizontal().GetScalar();
			dgFloat32 aRel = rhs->m_deltaAccel;

			if (rhs->m_normalForceIndex == DG_INDEPENDENT_ROW) {
				dgAssert (rhs->m_restitution >= 0.0f);
				dgAssert (rhs->m_restitution <= 2.0f);
				dgFloat32 restitution = (vRel <= dgFloat32 (0.0f)) ? (dgFloat32 (1.0f) + rhs->m_restitution) : dgFloat32 (1.0f);

				dgFloat32 penetrationVeloc = dgFloat32 (0.0f);
				if (rhs->m_penetration > DG_RESTING_CONTACT_PENETRATION * dgFloat32 (0.125f)) {
					if (vRel > dgFloat32 (0.0f)) {
						dgFloat32 penetrationCorrection = vRel * timestep;
						dgAssert (penetrationCorrection >= dgFloat32 (0.0f));
						rhs->m_penetration = dgMax (dgFloat32 (0.0f), rhs->m_penetration - penetrationCorrection);
					} else {
						dgFloat32 penetrationCorrection = -vRel * timestep * rhs->m_restitution * dgFloat32 (8.0f);
						if (penetrationCorrection > rhs->m_penetration) {
							rhs->m_penetration = dgFloat32 (0.001f);
						}
					}
					penetrationVeloc = -(rhs->m_penetration * rhs->m_penetrationStiffness);
				}

				vRel = vRel * restitution + penetrationVeloc;
				//vRel = dgMin (MAX_SEPARATING_SPEED, vRel);
			}

			const dgFloat32 relGyro = (jacobian0.m_angular * gyroAlpha0 + jacobian1.m_angular * gyroAlpha1).AddHorizontal().GetScalar();
			//dgFloat32 relGyro = dgFloat32 (0.0f);
			rhs->m_coordenateAccel = relGyro + aRel - vRel * invTimestep;
		}
	}
}

