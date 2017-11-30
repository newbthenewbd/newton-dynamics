/* Copyright (c) <2003-2016> <Newton Game Dynamics>
* 
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
* 
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely
*/

#include "toolbox_stdafx.h"
#include "SkyBox.h"
#include "DemoMesh.h"
#include "DemoCamera.h"
#include "DebugDisplay.h"
#include "PhysicsUtils.h"
#include "TargaToOpenGl.h"
#include "DemoEntityManager.h"
#include "dCustomBallAndSocket.h"
#include "HeightFieldPrimitive.h"


class dSixAxisController: public dCustomControllerBase
{
	public:
	class KukaServoMotor : public dCustomHinge
	{
		public:
		KukaServoMotor(const dMatrix& pinAndPivotFrame, NewtonBody* const child, NewtonBody* const parent)
			:dCustomHinge(pinAndPivotFrame, child, parent)
			,m_torque(1000.0f)
		{
		}

		void SubmitConstraintsFreeDof(dFloat timestep, const dMatrix& matrix0, const dMatrix& matrix1)
		{
			NewtonUserJointAddAngularRow(m_joint, 0.0f, &matrix1.m_front[0]);
			dFloat accel = NewtonUserJointGetRowInverseDynamicsAcceleration(m_joint);
			NewtonUserJointSetRowAcceleration(m_joint, accel);
			NewtonUserJointSetRowMinimumFriction(m_joint, -m_torque);
			NewtonUserJointSetRowMaximumFriction(m_joint, m_torque);
		}

		dFloat m_torque;
	};


	void PreUpdate(dFloat timestep, int threadIndex)
	{
	}
	void PostUpdate(dFloat timestep, int threadIndex){}

};

class dSixAxisManager: public dCustomControllerManager<dSixAxisController>
{
	public:
	dSixAxisManager(DemoEntityManager* const scene)
		:dCustomControllerManager<dSixAxisController>(scene->GetNewton(), "sixAxisManipulator")
		,m_azimuth(0.0f)
		,m_posit_x(0.0f)
		,m_posit_y(0.0f)
	{
		scene->Set2DDisplayRenderFunction(RenderHelpMenu, NULL, this);
	}

	~dSixAxisManager()
	{
	}

	static void RenderHelpMenu(DemoEntityManager* const scene, void* const context)
	{
		dSixAxisManager* const me = (dSixAxisManager*)context;
		me->DrawHelp(scene);
	}

	void DrawHelp(DemoEntityManager* const scene)
	{
		dVector color(1.0f, 1.0f, 0.0f, 0.0f);
		scene->Print(color, "Use Sliders to manipulate the robot");
		ImGui::SliderFloat("Azimuth", &m_azimuth, -360.0f, 360.0f);
		ImGui::SliderFloat("position_x", &m_posit_x, -1.0f, 1.0f);
		ImGui::SliderFloat("position_y", &m_posit_y, -1.0f, 1.0f);
	}
	
	void MakeKukaRobot(DemoEntityManager* const scene, const dVector& origin)
	{
		dMatrix location(dRollMatrix(90.0f * 3.141592f / 180.0f));
		location.m_posit = origin;
		location.m_posit.m_y += 0.125f * 0.5f;

		NewtonBody* const parent = CreateCylinder(scene, location, 0.35f, 0.125f);
		NewtonBodySetMassMatrix(parent, 0.0f, 0.0f, 0.0f, 0.0f);

		dMatrix baseMatrix(dGetIdentityMatrix());
		baseMatrix.m_posit = location.m_posit;
		baseMatrix.m_posit.m_y += 0.125f * 0.5f + 0.11f;
		baseMatrix.m_posit.m_z += 0.125f * 0.5f;

		NewtonBody* const base = CreateBox(scene, baseMatrix, dVector(0.125f, 0.2f, 0.25f));

		dMatrix baseSpin(dGrammSchmidt(dVector(0.0f, 1.0f, 0.0f)));
		baseSpin.m_posit = location.m_posit;
		dSixAxisController::KukaServoMotor* const baseHinge = new dSixAxisController::KukaServoMotor(baseSpin, base, parent);

		dMatrix arm0Matrix(dPitchMatrix(45.0f * 3.141592f / 180.0f));
		arm0Matrix.m_posit = baseMatrix.m_posit;
		arm0Matrix.m_posit.m_y += 0.30f;
		arm0Matrix.m_posit.m_x += 0.09f;
		arm0Matrix.m_posit.m_z -= 0.125f;

		NewtonBody* const arm0 = CreateBox(scene, arm0Matrix, dVector(0.05f, 0.1f, 0.75f));
		dMatrix arm0HingeMatrix(dGrammSchmidt(dVector(1.0f, 0.0f, 0.0f)));
		arm0HingeMatrix.m_posit = arm0Matrix.m_posit + arm0Matrix.RotateVector(dVector(0.0f, 0.0f, 0.3f));
		dSixAxisController::KukaServoMotor* const arm0Hinge = new dSixAxisController::KukaServoMotor(arm0HingeMatrix, arm0, base);

		dMatrix arm1Matrix(arm0Matrix * dYawMatrix(3.131592f));
		arm1Matrix.m_posit = arm0Matrix.m_posit;
		arm1Matrix.m_posit.m_y += 0.4f;
		arm1Matrix.m_posit.m_x -= 0.05f;
		arm1Matrix.m_posit.m_z -= 0.1f;
		NewtonBody* const arm1 = CreateBox(scene, arm1Matrix, dVector(0.05f, 0.1f, 0.5f));

		dMatrix arm1HingeMatrix(dGrammSchmidt(dVector(1.0f, 0.0f, 0.0f)));
		arm1HingeMatrix.m_posit = arm1Matrix.m_posit + arm1Matrix.RotateVector(dVector(0.0f, 0.0f, 0.2f));
		dSixAxisController::KukaServoMotor* const arm1Hinge = new dSixAxisController::KukaServoMotor(arm1HingeMatrix, arm1, arm0);
	}

	private:
	void ScaleIntertia(NewtonBody* const body, dFloat factor) const
	{
		dFloat Ixx;
		dFloat Iyy;
		dFloat Izz;
		dFloat mass;
		NewtonBodyGetMass(body, &mass, &Ixx, &Iyy, &Izz);
		NewtonBodySetMassMatrix(body, mass, Ixx * factor, Iyy * factor, Izz * factor);
	}

	NewtonBody* CreateBox(DemoEntityManager* const scene, const dMatrix& location, const dVector& size) const
	{
		NewtonWorld* const world = scene->GetNewton();
		int materialID = NewtonMaterialGetDefaultGroupID(world);
		NewtonCollision* const collision = CreateConvexCollision(world, dGetIdentityMatrix(), size, _BOX_PRIMITIVE, 0);
		DemoMesh* const geometry = new DemoMesh("primitive", collision, "smilli.tga", "smilli.tga", "smilli.tga");

		dFloat mass = 1.0f;
		NewtonBody* const body = CreateSimpleSolid(scene, geometry, mass, location, collision, materialID);
		ScaleIntertia(body, 10.0f);

		geometry->Release();
		NewtonDestroyCollision(collision);
		return body;
	}

	NewtonBody* CreateCylinder(DemoEntityManager* const scene, const dMatrix& location, dFloat radius, dFloat height) const
	{
		NewtonWorld* const world = scene->GetNewton();
		int materialID = NewtonMaterialGetDefaultGroupID(world);
		dVector size(radius, height, radius, 0.0f);
		NewtonCollision* const collision = CreateConvexCollision(world, dGetIdentityMatrix(), size, _CYLINDER_PRIMITIVE, 0);
		DemoMesh* const geometry = new DemoMesh("primitive", collision, "smilli.tga", "smilli.tga", "smilli.tga");

		dFloat mass = 1.0f;
		NewtonBody* const body = CreateSimpleSolid(scene, geometry, mass, location, collision, materialID);
		ScaleIntertia(body, 10.0f);
		
		geometry->Release();
		NewtonDestroyCollision(collision);
		return body;
	}

	dFloat32 m_azimuth;
	dFloat32 m_posit_x;
	dFloat32 m_posit_y;
};


void SixAxisManipulators(DemoEntityManager* const scene)
{
	// load the sky box
	scene->CreateSkyBox();
	CreateLevelMesh (scene, "flatPlane.ngd", true);
	dSixAxisManager* const robotManager = new dSixAxisManager(scene);

	robotManager->MakeKukaRobot (scene, dVector (0.0f, 0.0f, 0.0f));
//	robotManager->MakeKukaRobot (scene, dVector (0.0f, 0.0f, 2.0f));
	dVector origin(-10.0f, 1.0f, 0.0f, 1.0f);
	origin.m_x = -5.0f;
//	origin.m_x -= 2.0f;
	origin.m_y  = 0.5f;
	dQuaternion rot;
	scene->SetCameraMatrix(rot, origin);
}

