// C++ Headers
#include <cmath>

// ObjexxFCL Headers
#include <ObjexxFCL/Fmath.hh>

// EnergyPlus Headers
#include <EcoRoofManager.hh>
#include <ConductionTransferFunctionCalc.hh>
#include <ConvectionCoefficients.hh>
#include <DataEnvironment.hh>
#include <DataGlobals.hh>
#include <DataHeatBalance.hh>
#include <DataHeatBalFanSys.hh>
#include <DataHeatBalSurface.hh>
#include <DataLoopNode.hh>
#include <DataPrecisionGlobals.hh>
#include <DataSurfaces.hh>
#include <DataWater.hh>
#include <General.hh>
#include <OutputProcessor.hh>
#include <Psychrometrics.hh>
#include <UtilityRoutines.hh>

namespace EnergyPlus {

namespace EcoRoofManager {
	// Module containing the heat balance simulation routines
	// calculation (initialization) routines

	// MODULE INFORMATION:
	//       AUTHOR         David Sailor and Toan Pham, Portland State University
	//       DATE WRITTEN   Jan 2007
	//       MODIFIED       Oct 2010
	//       RE-ENGINEERED  na

	// PURPOSE OF THIS MODULE:
	// Module for implementing an ecoroof (aka Green Roof)

	// METHODOLOGY EMPLOYED:
	// Vikram Madhusudan's Portland State Univ. MS Thesis (Dec 2005) based on FASST model
	// of Frankenstein and Koenig (2004) - DRDC/CRREL Technical Report TR-04-25.
	// Precipitation schedules and irrigation schedules can be used to define hourly moisture
	// inputs (m). Moisture transport updated Oct 2010.
	// REFERENCES:
	// OTHER NOTES:

	// USE STATEMENTS:
	// Use statements for data only modules
	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using namespace DataSurfaces;
	using namespace DataGlobals;
	using namespace DataLoopNode;
	using namespace DataHeatBalance;
	using DataWater::RainFall;
	using DataWater::Irrigation;
	using DataWater::IrrSchedDesign;
	using DataWater::IrrSmartSched;
	using DataWater::RainSchedDesign;
	// Use statements for access to subroutines in other modules
	using namespace ConductionTransferFunctionCalc;

	// Data
	// MODULE PARAMETER DEFINITIONS
	// na

	// DERIVED TYPE DEFINITIONS
	// na

	// MODULE VARIABLE DECLARATIONS:

	Real64 CumRunoff( 0.0 ); // Cumulative runoff, updated each time step (m) mult by roof area to get volume
	Real64 CumET( 0.0 ); // Cumulative evapotranspiration from soil and plants (m)
	Real64 CumPrecip( 0.0 );
	Real64 CumIrrigation( 0.0 ); // Cumulative irrigation, updated each time step (m) mult by roof area to get volume
	Real64 CurrentRunoff;
	Real64 CurrentET;
	Real64 CurrentPrecipitation; // units of (m) per timestep
	Real64 CurrentIrrigation; // units of (m) per timestep

	Real64 Tfold; // leaf temperature from the previous time step
	Real64 Tgold; // ground temperature from the previous time step
	bool EcoRoofbeginFlag( true );
	
	// MODULE SUBROUTINES:

	//*************************************************************************

	// Functions
	
	void
	GreenRoof_with_PlantCoverage(
		int const SurfNum, // Indicator of Surface Number for the current surface
		int const ZoneNum, // Indicator for zone number where the current surface
		int & ConstrNum, // Indicator for construction index for the current surface
        Real64 & TempExt // Exterior temperature boundary condition
	)
    {

		// SUBROUTINE INFORMATION
       // AUTHOR          Neda Yaghoobian, University of Maryland, College Park
       // DATE WRITTEN    March 2014
       // MODIFIED        na
       // RE-ENGINEERED   na
       // This subroutine takes into account plant coverage in simulating the green roof energy balance
       // Reference: 1)TABARES-VELASCO, P.C. and SREBRIC, J., 2012. A heat transfer model for assessment
       //             of plant based roofing systems in summer conditions. Building and Environment, 49, pp. 310-323.
       //           2)Yaghoobian, N. and Srebric, J., 2014. Influence of Green Roof Plant Coverage on
       //             the Total Roof Energy Balance and Building Energy Consumption. Applied Energy
  
       // PURPOSE OF THIS SUBROUTINE:
       // To find the area-averaged substrate surface temperature of the green roof
       // to be used as the outside roof surface temperature in the building energy simulation
  
       // METHODOLOGY EMPLOYED:
       // The energy balance equations for plants, bare soil surface, and substrate surface under
      // the plant layer are solved iteratively for their temperatures by Newton’s method. Then,
      // using the plant coverage percentage the area-averaged soil surface temperature is calculated
      // to be used as the roof surface temperature in the conduction calculation process (Conduction
      // Transfer Functions; CTFs) of EnergyPlus, taking into account all layers of the roof construction.
      
	    using namespace DataGlobals;
		using namespace DataEnvironment;
		using namespace DataHeatBalFanSys;
		using namespace DataHeatBalance;
		using namespace DataHeatBalSurface;
		using namespace DataSurfaces;
		//  USE DataDaylightingDevices
		//  USE DaylightingDevices,        ONLY: FindTDDPipe
		using namespace Psychrometrics;
		using ConvectionCoefficients::InitExteriorConvectionCoeff;
		using ConvectionCoefficients::SetExtConvectionCoeff;
		using ConvectionCoefficients::SetIntConvectionCoeff;
		
		// Locals
		//SUBROUTINE ARGUMENT DEFINITIONS:

		//SUBROUTINE PARAMETER DEFINITIONS:
		Real64 const Cp_air( 1005.0 ); //Specific heat of air (j/kg.K)
		Real64 const Le_num( 1.0 ); //Lewis number
		Real64 const phi( 0.85 ); //Porosity
		Real64 const k_air( 0.0267 ); //Thermal conductivity (W/m K) for air at 300 K (Mills 1999 Heat Transfer)
		Real64 const k_plants( 0.5 ); //Plants Thermal Conducvity (W/m K)
		Real64 const Rair( 0.286e3 ); //Gas Constant of air J/Kg K
		Real64 const Sigma( 5.6697e-08 ); //Stefan-Boltzmann constant W/m^2K^4
		
		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		static bool MyEnvrnFlag(true);
		static int FirstEcoSurf( 0 ); //Indicates lowest numbered surface that is an ecoroof
		Real64 RS;                  // shortwave radiation 
		Real64 Latm;               //Long Wave Radiation (W/m^2)
        Real64 Q_sol_abs_plants;  //Absorbed SW radiation by the plants
        Real64 Mg;               //Surface soil moisture content m^3/m^3 (Moisture / MoistureMax)
        static Real64 T_plant;  //Plant (leaf) temperature (K)
        static Real64 T_soil;  //Soil surface temperature (K)
        static Real64 T_bare_soil;    //Bare soil surface temperature (K)
		
	//***--------
		static Real64 Tsoil_avg;  //Average soil temperature [K]
		
		static Real64 Tsoil_avg_Rep;
		static Real64 T_plant_Rep;
		static Real64 Qconv_p_Rep;
		static Real64 Qconv_s_Rep;
		static Real64 Qconv_bare_s_Rep;
		static Real64 Qconv_s_avg_Rep;
		static Real64 Q_ET_p_Rep;
		static Real64 Q_E_s_Rep;
		static Real64 Q_E_bare_s_Rep;
		static Real64 Q_E_avg_Rep;
		static Real64 Q_sol_soil_Rep;
		static Real64 Q_sol_bare_s_Rep;
		static Real64 Q_sol_s_avg_Rep;
		static Real64 Q_IR_s_Rep;
		static Real64 Q_IR_bare_s_Rep;
		static Real64 Q_IR_s_avg_Rep;
		static Real64 Qcond_avg_Rep;
		
	//***--------
		Real64 i_fg;  // Latent heat of vaporization (j/kg)
		Real64 Pa;  // Standard atmospheric pressure (Pa)
		//Real64 h_conv;  // Convective heat transfer coefficient (W/m2-K)
		Real64 Ta;  //current air temperature (C)
		Real64 Tak;  //current air temperature (K)
		Real64 f_solar;  //Empirical multiplicative functions for solar irradiance role on stomatal aperture
		Real64 f_VWC;  //Empirical multiplicative functions for Substrate Volumetric Water Content
		Real64 f_VPD;  //Empirical multiplicative functions for vapor pressure deficit (VPD) role on stomatal 
		               //aperture (VWC or Moisture) role on stomatal aperture
		Real64 h_por;
		Real64 k_por;
		Real64 alpha_por;
		Real64 Pe;  //Peclet number
		Real64 NU_por;  //Nusselt number for porous media
		Real64 Var_1;
		Real64 Var_2;
		Real64 Var_a;
		Real64 Var_b;
		
		static Real64 LAI;  //Leaf Area Index
		static Real64 Alphag;  //Ground albedo
		static Real64 Alphap;  //Plant albedo
		static Real64 epsilong;  //Ground emisivity
		static Real64 epsilonp;  // Plant emisivity
		static Real64 Moisture;  // m^3/m^3.The moisture content in the soil is the value provided by a user
		static Real64 MoistureResidual;  // m^3/m^3. Residual & maximum water contents are unique to each material.
		static Real64 MoistureMax;           // Maximum volumetric moisture content (porosity) m^3/m^3
		static Real64 MeanRootMoisture;      // Mean value of root moisture m^3/m^3
		static Real64 SoilThickness;         // Soil thickness (m)
		static Real64 StomatalResistanceMin;  // s/m . ! Minimum stomatal resistance is unique for each veg. type.
		static Real64 sigma_f;    //Plant coverage

//***------------
		Real64 tau_sw;
		Real64 tau_lw;
		Real64 EpsilonOne;
		Real64 RH;
		static Real64 Vfluxf( 0.0 );  //Water evapotr. rate associated with latent heat from vegetation [m/s]
		static Real64 Vfluxg( 0.0 );  //Water evapotr. rate associated with latent heat from ground surface [m/s]
		static Real64 Alphag_UnUsed( 0.3 );  //Ground Albedo (From EcoRoof Model - Not used here)
		Real64 WS;
		Real64 Rhoa;
		Real64 eair;
		Real64 r_s_sub;
		Real64 Q_sol_abs_soil;
		Real64 Q_sol_abs_bare_soil;
		Real64 F1temp;
		Real64 Qsoilpart1;
		Real64 Qsoilpart2;
		Real64 Tp_new;
		Real64 Tp_old;
		Real64 Q_IR_sky_p;
		Real64 Q_IR_exch_p;
		static Real64 Qconv_p;
		Real64 r_a;
		Real64 r_a_bare;
		Real64 r_s;
		static Real64 Q_ET_p;
		Real64 Func_p;
		Real64 Func_prim_p;
		Real64 Ts_new;
		Real64 Ts_bare_new;
		Real64 Ts_old;
		Real64 Ts_bare_old;
		Real64 Q_IR_sky_s;
		Real64 Q_IR_sky_bare_s;
		Real64 Q_IR_exch_s;
		static Real64 Qconv_s;
		static Real64 Qconv_bare_s;
		Real64 r_a_sub;
		static Real64 Q_E_s;
		static Real64 Q_E_bare_s;
		static Real64 Q_E_avg;
		Real64 Qcond_s;
		Real64 Qcond_bare_s;
		Real64 Func_s;
		Real64 Func_bare_s;
		Real64 Q_E_S_prim;
		Real64 Q_E_bare_s_prim;
		Real64 Func_prim_s;
		Real64 Func_prim_bare_s;
		Real64 i_fg_p;  // Latent heat of vaporation at leaf surface temperature (J/kg)
		Real64 i_fg_g;          // Latent heat of vaporization  at the ground temperature (J/kg)
		int RoughSurf;       // Roughness index of the exterior surface.
		Real64 AbsThermSurf;    // Thermal absoptance of the exterior surface
		Real64 HMovInsul;       // "Convection" coefficient of movable insulation
		static Real64 VWC_fc;  // Substrate volumetric water content at field capacity
		static Real64 VWC_wp;          // Substrate volumetric water content at wilting point
		static Real64 Ksw;             // SW extinction coefficient
		static Real64 Klw;             // LW extinction coefficient
		
		Real64 length;
		static int unit( 0 );
		static Real64 Qsoil( 0.0 );  //heat flux from the soil layer
		Real64 NTest1;
		Real64 NTest2;
		Real64 NTest3;
		Real64 NTest4;
		Real64 NTest5;
		Real64 NTest6;
		int i;
		int iter1;
		int iter2;
		int iter3;
		int iter_Mid;
		Real64 Solution1 [500];
		Real64 Solution2 [500];
		Real64 Solution3 [500];
		Real64 Func1 [500];
		Real64 Func2 [500];
		Real64 Func3 [500];
		Real64 MidPoint;
		Real64 Func_MidPoint;
		Real64 Func_1;
		Real64 Func_2;
		Real64 Sol_1;
		Real64 Sol_2;
//**--------------------		

//***------------------
// FLOW:

		if(SurfaceWindow(SurfNum).StormWinFlag==1) ConstrNum = Surface(SurfNum).StormWinConstruction;
        RoughSurf = Material(Construct(ConstrNum).LayerPoint(1)).Roughness;
        AbsThermSurf = Material(Construct(ConstrNum).LayerPoint(1)).AbsorpThermal;
        HMovInsul = 0.0;

        if (Surface(SurfNum).ExtWind) { 
        InitExteriorConvectionCoeff(SurfNum,HMovInsul,RoughSurf,AbsThermSurf,TH(SurfNum,1,1), HcExtSurf(SurfNum), HSkyExtSurf(SurfNum), HGrdExtSurf(SurfNum), HAirExtSurf(SurfNum));
        } 			
		
// Solar radiation :		
        RS = BeamSolarRad + AnisoSkyMult(SurfNum) * DifSolarRad;
		
// Green roof length (from the area)
        length = sqrt(Surface(SurfNum).Area);
		
		
		if (EcoRoofbeginFlag) {
            EcoRoofbeginFlag = false;
    
    // ONLY READ ECOROOF PROPERTIES IN THE FIRST TIME
    
            LAI = Material(Construct(ConstrNum).LayerPoint( 1 )).LAI;             // Leaf Area Index
            Alphag = 1.0 - Material(Construct(ConstrNum).LayerPoint( 1 )).AbsorpSolar; // albedo rather than absorptivity
            Alphap = Material(Construct(ConstrNum).LayerPoint( 1 )).Lreflectivity;   // Leaf Reflectivity
            epsilonp = Material(Construct(ConstrNum).LayerPoint( 1 )).LEmissitivity;   // Leaf Emisivity
            StomatalResistanceMin = Material(Construct(ConstrNum).LayerPoint( 1 )).RStomata;   // Leaf min stomatal resistance
            epsilong = Material(Construct(ConstrNum).LayerPoint( 1 )).AbsorpThermal;   // Soil Emisivity
            MoistureMax = Material(Construct(ConstrNum).LayerPoint( 1 )).Porosity;   // Max moisture content in soil
            MoistureResidual = Material(Construct(ConstrNum).LayerPoint( 1 )).MinMoisture;   // Min moisture content in soil
            Moisture = Material(Construct(ConstrNum).LayerPoint( 1 )).InitMoisture;   // Initial moisture content in soil
            MeanRootMoisture = Moisture; // DJS Oct 2007 Release --> all soil at same initial moisture for Reverse DD fix
            SoilThickness = Material(Construct(ConstrNum).LayerPoint( 1 )).Thickness; // Total thickness of soil layer (m)

            sigma_f = Material(Construct(ConstrNum).LayerPoint( 1 )).PlantCoverage;
            VWC_fc = Material(Construct(ConstrNum).LayerPoint( 1 )).VWC_FieldCapacity;
            VWC_wp = MoistureResidual;
            Ksw = Material(Construct(ConstrNum).LayerPoint( 1 )).SW_ExtCoeff;
            Klw = Material(Construct(ConstrNum).LayerPoint( 1 )).LW_ExtCoeff;


            FirstEcoSurf = SurfNum;          // this determines WHEN to updatesoilProps

// DJS NOVEMBER 2010 - Make calls to SetupOutput Variable to allow for reporting of ecoroof variables

            SetupOutputVariable( "Green Roof Soil Temperature [C]", Tsoil_avg_Rep, "Zone", "State", "Environment" );
            SetupOutputVariable( "Green Roof Vegetation Temperature [C]", T_plant_Rep, "Zone", "State", "Environment" );
            SetupOutputVariable( "Green Roof Soil Root Moisture Ratio []", MeanRootMoisture, "Zone", "State", "Environment" );
            SetupOutputVariable( "Green Roof Soil Near Surface Moisture Ratio []", Moisture, "Zone", "State", "Environment" );
            SetupOutputVariable( "Green Roof Soil Sensible Heat Transfer Rate per Area [W/m2]", Qconv_s_avg_Rep, "Zone", "State", "Environment" );
            SetupOutputVariable( "Green Roof Vegetation Sensible Heat Transfer Rate per Area [W/m2]", Qconv_p_Rep, "Zone", "State", "Environment" );
            SetupOutputVariable( "Green Roof Vegetation Moisture Transfer Rate [m/s]", Vfluxf, "Zone", "State", "Environment" );
            SetupOutputVariable( "Green Roof Soil Moisture Transfer Rate [m/s]", Vfluxg, "Zone", "State", "Environment" );
            SetupOutputVariable( "Green Roof Vegetation Latent Heat Transfer Rate per Area [W/m2]", Q_ET_p_Rep, "Zone", "State", "Environment" );
            SetupOutputVariable( "Green Roof Soil Latent Heat Transfer Rate per Area [W/m2]", Q_E_avg_Rep, "Zone", "State","Environment" );

            SetupOutputVariable( "Green Roof Cumulative Precipitation Depth [m]", CumPrecip, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Cumulative Irrigation Depth [m]", CumIrrigation, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Cumulative Runoff Depth [m]", CumRunoff, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Cumulative Evapotranspiration Depth [m]", CumET, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Current Precipitation Depth [m]", CurrentPrecipitation, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Current Irrigation Depth [m]", CurrentIrrigation, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Current Runoff Depth [m]", CurrentRunoff, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Current Evapotranspiration Depth [m]", CurrentET, "Zone", "Sum", "Environment" );
    
    
// DJS NOVEMBER 2010 - end of calls to setup output of ecoroof variables

//***------------------
            SetupOutputVariable( "Green Roof Soil Net SW Rad [W/m2]", Q_sol_s_avg_Rep, "Zone", "State", "Environment" );
			SetupOutputVariable( "Green Roof Soil Net LW Rad [W/m2]", Q_IR_s_avg_Rep, "Zone", "State", "Environment" );
			SetupOutputVariable( "Green Roof Soil Conduction [W/m2]", Qcond_avg_Rep, "Zone", "State", "Environment" );
    
//***------------------

            }// Initialization statements for first entry into ecoroof routines
			
// DJS July 2007
// Make sure the ecoroof module resets its conditions at start of EVERY warmup day and every new design day
// for Reverse DD testing
        if (BeginEnvrnFlag || WarmupFlag) {
            Moisture = Material(Construct(ConstrNum).LayerPoint( 1 )).InitMoisture;   // Initial moisture content in soil
            MeanRootMoisture = Moisture;  // Start the root zone moisture at the same value as the surface.
            Alphag = 1.0 - Material(Construct(ConstrNum).LayerPoint( 1 )).AbsorpSolar; // albedo rather than absorptivity
        } 
// DJS July 2007			

        if (BeginEnvrnFlag && MyEnvrnFlag) {
            T_soil = OutDryBulbTempAt(Surface(SurfNum).Centroid.z) + KelvinConv;        //OutDrybulbTemp           // initial guess
            T_plant = OutDryBulbTempAt(Surface(SurfNum).Centroid.z) + KelvinConv;        //OutDrybulbTemp           // initial guess
            T_bare_soil = OutDryBulbTempAt(Surface(SurfNum).Centroid.z) + KelvinConv;   //OutDrybulbTemp           // initial guess
            Vfluxf = 0.0;
            Vfluxg = 0.0;
            CumRunoff = 0.0;
            CumET = 0.0;
            CumPrecip = 0.0;
            CumIrrigation = 0.0;
            CurrentRunoff = 0.0;
            CurrentET = 0.0;
            CurrentPrecipitation = 0.0;
            CurrentIrrigation = 0.0;
            MyEnvrnFlag = false;
        }
		
		if ( ! BeginEnvrnFlag ) {
            MyEnvrnFlag = true;
        }
		
//---Shortwave and logwave transmittance of a canopy:
        tau_sw = std::exp( -Ksw * LAI );
        tau_lw = std::exp( -Klw * LAI );

//---Denominator in LW exchange between plants & soil surface:
        EpsilonOne = epsilonp + epsilong - epsilong * epsilonp;


        RH= OutRelHum; // Get humidity in % from the DataEnvironment.f90
		
//From EcoRoof: If current surface is = FirstEcoSurf then for this time step we need to update the soil moisture
//----'FirstEcoSurf' gets its value when 'EcoRoofbeginFlag' is TRUE (first entry into ecoroof routines)

        if (SurfNum == FirstEcoSurf) {
//----NOTE: unit, T_soil, T_plant, Qsoil are unused in 'UpdateSoilProps' subroutine.
           UpdateSoilProps( Moisture, MeanRootMoisture, MoistureMax, MoistureResidual, SoilThickness, Vfluxf, Vfluxg, ConstrNum, Alphag_UnUsed, unit, T_soil, T_plant, Qsoil );

//---Soil albedo
           Mg = Moisture / MoistureMax;
           Alphag = 0.2171 * pow_2(Mg) - 0.4336 * Mg + 0.3143;
    
           WS = WindSpeedAt(Surface(SurfNum).Centroid.z);         // Windspeed at Z of roof
           Ta = OutDryBulbTempAt(Surface(SurfNum).Centroid.z);    // Temperature outdoor - Surface is dry, use normal correlation
           Tak = Ta + KelvinConv;
           Pa = StdBaroPress;                                     // standard atmospheric pressure (apparently in Pascals)
           Rhoa = Pa /( Rair * Tak );                                  // Density of air. kg/m^3

//---eair
           eair = ( RH / 100.0) * e_s(Tak);    

//---r_s_sub    
           r_s_sub = 34.52 * std::pow(Mg,-3.2678);

//---Absorbed shortwave radiation 
           Q_sol_abs_plants = ( 1 - Alphap - tau_sw ) * (1 + tau_sw * Alphag ) * RS;       //by the plants
           Q_sol_abs_soil = tau_sw * ( 1 - Alphag ) * RS;                                //by the soil surface - Covered by plants
           Q_sol_abs_bare_soil = ( 1 - Alphag ) * RS;                                    //by the bare soil surface

//---f_solar
           f_solar = 1 + std::exp( -0.034 * ( RS - 3.5));
    
//---f_VWC
           if ( Moisture > 0.7*VWC_fc ) {
               f_VWC = 1;
		   } 	   
           else {
               f_VWC = max(0.0,1/((Moisture - VWC_wp)/(0.7*VWC_fc - VWC_wp))); }
        }
        if (Moisture < VWC_wp) {
            f_VWC = 1000;
        } 
    
//---h_por 
        k_por = phi*k_air + (1.-phi)*k_plants;  //Porous media thermal conductivity
        alpha_por = k_por/(Rhoa*Cp_air);
        Pe = 0.3*WS*length/alpha_por;           //Peclet number
        NU_por = 1.128 * std::sqrt(Pe);               //Nusselt number for porous media
        h_por = NU_por*k_por/length;
		
//---Conduction based on EcoRoof subroutine 
        if ( Construct( ConstrNum ).CTFCross( 0 ) > 0.01) {
            F1temp = Construct( ConstrNum ).CTFCross( 0 ) / ( Construct( ConstrNum ).CTFInside( 0 ) + HConvIn( SurfNum ) );
            Qsoilpart1 = -CTFConstOutPart( SurfNum ) + F1temp * ( CTFConstInPart( SurfNum ) + QRadSWInAbs( SurfNum ) + QRadThermInAbs( SurfNum ) + Construct( ConstrNum ).CTFSourceIn( 0 ) * QsrcHist( SurfNum,1 ) + HConvIn( SurfNum ) * MAT( ZoneNum ) + NetLWRadToSurf( SurfNum ) );
        } 
		else {
			Qsoilpart1 = -CTFConstOutPart( SurfNum ) + Construct( ConstrNum ).CTFCross( 0 ) * TempSurfIn( SurfNum );
            F1temp = 0.0;
        }
        Qsoilpart2 = Construct( ConstrNum ).CTFOutside( 0 ) - F1temp * Construct( ConstrNum ).CTFCross( 0 );


//---Newton's method for solving T_plant
        if ( sigma_f != 0.0) {
            Tp_new = T_plant;
            Tp_old = Tp_new + 999;
            iter1 = 0;

            do{ 
               iter1 = iter1 + 1;
               Tp_old = Tp_new;
        
        //Assuming that the sky emissivity is equal to the plant emissivity
               Q_IR_sky_p = ( 1-tau_lw ) * epsilonp * Sigma * ( Surface( SurfNum ).ViewFactorSky * pow_4(SkyTempKelvin) - pow_4(Tp_old) - ( 1-epsilonp ) * Surface( SurfNum ).ViewFactorSky * pow_4(SkyTempKelvin));
               Q_IR_exch_p= ( 1-tau_lw) * Sigma * epsilonp * epsilong * ( pow_4(T_soil) - pow_4(Tp_old) )/ EpsilonOne;
               Qconv_p = LAI * h_conv( SurfNum,Tak,Tp_old,WS,k_air ) * ( Tp_old - Tak );
               r_a = Rhoa * Cp_air * std::pow(Le_num,2/3) / h_conv( SurfNum,Tak,Tp_old,WS,k_air );  //Aerodynamic resistance to 
                                                                                                  //mass transfer, s/m
               r_s = ( StomatalResistanceMin/LAI ) * f_solar * f_Hum( Tp_old,eair ) * f_VWC * f_temp( Tp_old );
               Q_ET_p = ( LAI * Rhoa * Cp_air / gamma_s( T_soil,Cp_air,Pa )) * (e_s( Tp_old ) - eair)/( r_s+r_a );


               Func_p = Q_sol_abs_plants + Q_IR_sky_p + Q_IR_exch_p - Qconv_p - Q_ET_p;

               Var_1 = std::exp( 17.27 * ( Tp_old-KelvinConv ) / (( Tp_old-KelvinConv ) + 237.3 ));
               Var_2 = 0.0016 * pow_2( 35.0 - Tp_old + KelvinConv ) - 1.0;
               Var_a = ( LAI * Rhoa * Cp_air / gamma_s( T_soil,Cp_air,Pa ));
               Var_b = ( StomatalResistanceMin / LAI ) * f_solar * f_Hum( Tp_old,eair ) * f_VWC;

               Func_prim_p = -4.0 * ( 1.0-tau_lw ) * epsilonp * Sigma * pow_3(Tp_old) - 4.0 * ( 1-tau_lw ) * Sigma * epsilonp * epsilong * pow_3(Tp_old) / EpsilonOne - LAI * h_conv( SurfNum,Tak,Tp_old,WS,k_air ) - ( Var_a * 0.6108 * Var_1 * ( 17.27 / ( Tp_old-KelvinConv + 237.3 ) - 17.27 * ( Tp_old - KelvinConv ) / pow_2( Tp_old - KelvinConv + 237.3)) / ( r_a + Var_b / abs(Var_2))) + Var_a * Var_b * 0.0016 * copysign(Var_2,1.0) * ( eair - 0.6108 * Var_1 ) * ( 2.0 * 35.0 - 2.0 * Tp_old + 2.0 * KelvinConv ) / ( pow_2(abs(Var_2)) * ( r_a + Var_b / pow_2(abs(Var_2))));

               Tp_new = Tp_old - Func_p / Func_prim_p; 

               Solution1[iter1] = Tp_old;
               Func1[iter1] = Func_p;
               MidPoint = 0.5 * ( Solution1[iter1] + Solution1[iter1-1] );		
			   
			
			if(iter1 >= 100) {   // Means if Newton's Method entered an infinite loop use Bisection method instead
               if((( Func1[iter1] < 0.0 ) && ( Func1[iter1-1] > 0.0 )) || (( Func1[iter1] > 0.0 ) && ( Func1[iter1-1] < 0.0))) {
         // Bisection Method:
                    iter_Mid = 0;
                    do{ 
                        iter_Mid = iter_Mid + 1;

                        if(iter_Mid==1) {
                           Func_1 = Func1[iter1];
                           Func_2 = Func1[iter1-1];
                           Sol_1 = Solution1[iter1];
                           Sol_2 = Solution1[iter1-1];
                        } 
                        if (iter_Mid > 1) MidPoint = Tp_new;

          //Assuming that the sky emissivity is equal to the plant emissivity
                        Q_IR_sky_p = ( 1-tau_lw ) * epsilonp * Sigma * ( Surface( SurfNum ).ViewFactorSky * pow_4(SkyTempKelvin) - pow_4(MidPoint) - ( 1-epsilonp ) * Surface( SurfNum ).ViewFactorSky * pow_4(SkyTempKelvin));
                        Q_IR_exch_p = ( 1-tau_lw ) * Sigma * epsilonp * epsilong * ( pow_4(T_soil) - pow_4(MidPoint) ) / EpsilonOne;
                        Qconv_p = LAI * h_conv(SurfNum,Tak,MidPoint,WS,k_air) * ( MidPoint - Tak );
                        r_a = Rhoa * Cp_air * std::pow(Le_num,2/3) / h_conv(SurfNum,Tak,MidPoint,WS,k_air);
                        r_s = ( StomatalResistanceMin / LAI ) * f_solar * f_Hum(MidPoint,eair) * f_VWC * f_temp(MidPoint);
                        Q_ET_p = ( LAI * Rhoa * Cp_air / gamma_s(T_soil,Cp_air,Pa)) * (e_s(MidPoint) - eair) / ( r_s + r_a );
                        Func_MidPoint = Q_sol_abs_plants + Q_IR_sky_p + Q_IR_exch_p - Qconv_p - Q_ET_p;

                        if(((Func_MidPoint < 0.0) && (Func_1 > 0.0)) || ((Func_MidPoint > 0.0) && (Func_1 < 0.0))) {
                             Tp_new = 0.5 * ( MidPoint + Sol_1 );
                             Func_2 = Func_MidPoint;
                             Sol_2 = MidPoint;
						}	 
                        else if(((Func_MidPoint < 0.0) && (Func_2 > 0.0)) || ((Func_MidPoint > 0.0) && (Func_2 < 0.0))) {
                                  Tp_new = 0.5 * ( MidPoint + Sol_2 );
                                  Func_1 = Func_MidPoint;
                                  Sol_1 = MidPoint;
                        }
                    }while( abs(Tp_new - MidPoint) > 0.0001);
					
					goto Label1100;
                    }
				else {
                    goto Label1100;
                }
            }				
		    }while( abs(Tp_new - Tp_old) > 0.0001);
			
			Label1100:;
			T_plant = Tp_new;
            
            r_a = Rhoa * Cp_air * std::pow(Le_num,2/3) / h_conv( SurfNum, Tak, T_plant, WS, k_air );
            r_s = ( StomatalResistanceMin / LAI ) * f_solar * f_Hum( T_plant, eair ) * f_VWC * f_temp( T_plant );
            Q_ET_p = ( LAI * Rhoa * Cp_air / gamma_s( T_soil, Cp_air, Pa )) * (e_s( T_plant ) - eair ) / ( r_s + r_a );
            Q_ET_p_Rep = (LAI * Rhoa * Cp_air / gamma_s( T_soil, Cp_air,Pa )) * (e_s(T_plant) - eair) / ( r_s + r_a );
            Qconv_p_Rep = LAI * h_conv( SurfNum, Tak, T_plant, WS, k_air ) * ( T_plant - Tak );
        }

//################################################################
//---Newton's method for solving T_soil covered by plants	

        if( sigma_f != 0.0) {
           Ts_new = T_soil;
           Ts_old = Ts_new + 999;
           iter2 = 0;
           do {
              iter2 = iter2 + 1;
              Ts_old = Ts_new;
        
              //Assuming that the sky emissivity is equal to the soil emissivity
              Q_IR_sky_s = tau_lw * epsilong * Sigma * (Surface( SurfNum ).ViewFactorSky * pow_4(SkyTempKelvin) - pow_4(Ts_old) - ( 1 - epsilong ) * Surface( SurfNum ).ViewFactorSky * pow_4(SkyTempKelvin));
              Q_IR_exch_s = ( 1 - tau_lw ) * Sigma * epsilonp * epsilong * (pow_4(T_plant) - pow_4(Ts_old)) / EpsilonOne;
              Qconv_s = ( h_por * h_conv( SurfNum, Tak, T_plant, WS, k_air ) / ( h_por + h_conv( SurfNum, Tak, T_plant, WS, k_air )))*( Ts_old - Tak );

              r_a_sub = Rhoa * Cp_air * std::pow(Le_num,2/3) * ( 1 / h_por + 1 / h_conv( SurfNum, Tak, T_plant, WS, k_air ));
              Q_E_s = max(0.0, Rhoa * Cp_air / gamma_s( Ts_old, Cp_air, Pa ) * (e_s(Ts_old) - eair ) / ( r_s_sub + r_a_sub ));

              Qcond_s = -Qsoilpart1 + Qsoilpart2 * ( sigma_f * ( Ts_old - KelvinConv ) + (1 - sigma_f) * ( T_bare_soil - KelvinConv ));
        
              Func_s = Q_sol_abs_soil + Q_IR_sky_s + Q_IR_exch_s - Qconv_s - Q_E_s - Qcond_s;

              if ( Q_E_s == 0.0) {
                   Q_E_S_prim = 0.0;
			  }
			  else {
                   Q_E_S_prim = (( Rhoa * Cp_air / ( r_s_sub + r_a_sub )) * 0.6108 * 0.622 * pow_2(1000.0) * std::exp(-( 17.27 * ( KelvinConv - Ts_old )) / ( Ts_old - KelvinConv + 237.3 )) * ( 17.27 / ( Ts_old - KelvinConv + 237.3 ) + ( 17.27 * ( KelvinConv - Ts_old )) / pow_2(Ts_old - KelvinConv + 237.3)) * ( 2501.1 - (-2.3793) * ( KelvinConv - Ts_old ))) / ( Cp_air * Pa ) - (( Rhoa * Cp_air / ( r_s_sub + r_a_sub )) * 0.622 * (-2.3793) * pow_2(1000.0) * ( eair - 0.6108 * std::exp(-(17.27 * ( KelvinConv - Ts_old )) / ( Ts_old - KelvinConv + 237.3 )))) / ( Cp_air * Pa );
              }
              Func_prim_s = -4.0 * Sigma * pow_3(Ts_old) * epsilong * tau_lw + ( 4.0 * Sigma * pow_3(Ts_old) * epsilong * epsilonp * ( tau_lw - 1.0 )) / EpsilonOne - ( h_conv( SurfNum, Tak, T_plant, WS, k_air ) * h_por ) / ( h_conv( SurfNum, Tak, T_plant, WS, k_air ) + h_por ) - Q_E_S_prim - Qsoilpart2 * sigma_f;

              Ts_new = Ts_old - Func_s / Func_prim_s; 

              Solution2[iter2] = Ts_old;
              Func2[iter2] = Func_s;
              MidPoint = 0.5 * ( Solution2[iter2] + Solution2[iter2-1]);
              if (iter2 >= 100) {   // Means if Newton's Method entered an infinite loop use Bisection method instead
				if (((Func2[iter2] < 0.0) && (Func2[iter2-1] > 0.0)) || ((Func2[iter2] > 0.0) && (Func2[iter2-1] < 0.0))) {
                // Bisection Method:
                      iter_Mid = 0;
                      do { 
                          iter_Mid = iter_Mid + 1;

                          if (iter_Mid == 1) {
                              Func_1 = Func2[iter2];
                              Func_2 = Func2[iter2-1];
                              Sol_1  = Solution2[iter2];
                              Sol_2  = Solution2[iter2-1];
                          }   
                          if (iter_Mid > 1) MidPoint = Ts_new;


                      //Assuming that the sky emissivity is equal to the soil emissivity
                      Q_IR_sky_s  = tau_lw * epsilong * Sigma * (Surface(SurfNum).ViewFactorSky * pow_4(SkyTempKelvin) - pow_4(MidPoint) - ( 1 - epsilong ) * Surface(SurfNum).ViewFactorSky * pow_4(SkyTempKelvin));
                      Q_IR_exch_s = ( 1- tau_lw ) * Sigma * epsilonp * epsilong * ( pow_4(T_plant) - pow_4(MidPoint) ) / EpsilonOne;
                      Qconv_s = ( h_por * h_conv( SurfNum, Tak, T_plant, WS, k_air ) / ( h_por + h_conv( SurfNum, Tak, T_plant, WS, k_air ))) * ( MidPoint - Tak );
                      r_a_sub = Rhoa * Cp_air * std::pow(Le_num,2/3) * ( 1 / h_por + 1 / h_conv( SurfNum, Tak, T_plant, WS, k_air ));
                      Q_E_s = max( 0.0, Rhoa * Cp_air / gamma_s( MidPoint, Cp_air, Pa) * (e_s(MidPoint) - eair) / ( r_s_sub + r_a_sub ));
                      Qcond_s = -Qsoilpart1 + Qsoilpart2 * ( sigma_f * ( MidPoint - KelvinConv ) + ( 1 - sigma_f ) * ( T_bare_soil - KelvinConv ));
                      Func_MidPoint = Q_sol_abs_soil + Q_IR_sky_s + Q_IR_exch_s - Qconv_s - Q_E_s - Qcond_s;

                      if (((Func_MidPoint < 0.0) && (Func_1 > 0.0)) || ((Func_MidPoint > 0.0) && (Func_1 < 0.0))) {
                            Ts_new = 0.5 * ( MidPoint + Sol_1 );
                            Func_2 = Func_MidPoint;
                            Sol_2 = MidPoint;
					  }		
                      else if (((Func_MidPoint < 0.0) && (Func_2 > 0.0)) || ((Func_MidPoint > 0.0) && (Func_2 < 0.0))) {
                            Ts_new = 0.5 * ( MidPoint + Sol_2 );
                            Func_1 = Func_MidPoint;
                            Sol_1 = MidPoint;
                      }
                      }while( abs(Ts_new - MidPoint) > 0.0001 );
                      goto Label2200;
                     }
                else { 
                     goto Label2200;                
                }
			  }	
           }while( abs(Ts_new - Ts_old) > 0.0001 );
           Label2200:;
		   T_soil  = Ts_new;
           Q_E_s = max( 0.0, Rhoa * Cp_air / gamma_s( T_soil, Cp_air, Pa) * ( e_s(T_soil) - eair ) / ( r_s_sub + r_a_sub ));
           Q_E_s_Rep = max(0.0, Rhoa* Cp_air / gamma_s( T_soil, Cp_air, Pa ) * ( e_s(T_soil) - eair ) / ( r_s_sub + r_a_sub ));
           Qconv_s_Rep = ( h_por * h_conv( SurfNum, Tak, T_plant, WS, k_air ) / ( h_por + h_conv( SurfNum, Tak, T_plant, WS, k_air ))) * ( T_soil - Tak );
           Q_sol_soil_Rep = tau_sw *( 1 - Alphag ) * RS;
           Q_IR_s_Rep = tau_lw * epsilong * Sigma * ( Surface(SurfNum).ViewFactorSky * pow_4(SkyTempKelvin) - pow_4(T_soil) - ( 1 - epsilong ) * Surface(SurfNum).ViewFactorSky * pow_4(SkyTempKelvin)) + ( 1 - tau_lw ) * Sigma * epsilonp * epsilong * ( pow_4(T_plant) - pow_4(T_soil) ) / EpsilonOne;
		}
		
//################################################################
//---Newton's method for solving T_bare_soil
    if (sigma_f != 1) {
       Ts_bare_new = T_bare_soil;
       Ts_bare_old = Ts_bare_new + 999;

       iter3 = 0;
        do {
           iter3 = iter3 + 1;

           Ts_bare_old = Ts_bare_new;
        //Assuming that the sky emissivity is equal to the bare soil emissivity
           Q_IR_sky_bare_s = epsilong * Sigma * (Surface(SurfNum).ViewFactorSky * pow_4(SkyTempKelvin) - pow_4(Ts_bare_old) - (1 - epsilong) * Surface(SurfNum).ViewFactorSky * pow_4(SkyTempKelvin));
           Qconv_bare_s = h_conv_bare( SurfNum, Tak, Ts_bare_old, WS, k_air ) * ( Ts_bare_old - Tak );
           r_a_bare = Rhoa * Cp_air * std::pow(Le_num,2/3) / h_conv_bare( SurfNum, Tak, Ts_bare_old, WS, k_air );
           Q_E_bare_s = Rhoa * Cp_air / gamma_s( Ts_bare_old, Cp_air, Pa ) * ( e_s(Ts_bare_old) - eair ) / ( r_s_sub + r_a_bare );
           Qcond_bare_s = -Qsoilpart1 + Qsoilpart2 * ( sigma_f * ( T_soil - KelvinConv ) + ( 1 - sigma_f ) * ( Ts_bare_old - KelvinConv ));

           Func_bare_s = Q_sol_abs_bare_soil + Q_IR_sky_bare_s - Qconv_bare_s - Q_E_bare_s - Qcond_bare_s;
           Q_E_bare_s_prim = (( Rhoa * Cp_air / ( r_s_sub + r_a_bare )) * 0.6108 * 0.622 * pow_2(1000) * std::exp(-(17.27 * ( KelvinConv - Ts_bare_old )) / ( Ts_bare_old - KelvinConv + 237.3 )) * ( 17.27 / ( Ts_bare_old - KelvinConv + 237.3 ) + ( 17.27 * ( KelvinConv - Ts_bare_old )) / pow_2(Ts_bare_old - KelvinConv + 237.3)) * ( 2501.1 - (-2.3793) * ( KelvinConv - Ts_bare_old ))) / ( Cp_air * Pa ) - (( Rhoa * Cp_air / ( r_s_sub + r_a_bare )) * 0.622 * (-2.3793) * pow_2(1000) * ( eair - 0.6108 * std::exp(-(17.27 * ( KelvinConv - Ts_bare_old )) / ( Ts_bare_old - KelvinConv + 237.3 )))) / ( Cp_air * Pa );
           Func_prim_bare_s = -4.0 * Sigma * pow_3(Ts_bare_old) * epsilong - h_conv_bare( SurfNum, Tak, Ts_bare_old, WS, k_air ) - Q_E_bare_s_prim - Qsoilpart2 * ( 1 - sigma_f );
           Ts_bare_new = Ts_bare_old - Func_bare_s / Func_prim_bare_s;

           Solution3[iter3] = Ts_bare_old;
           Func3[iter3] = Func_bare_s;
           MidPoint = 0.5 * ( Solution3[iter3] + Solution3[iter3-1] );
           if ( iter3 >= 100 ) {  // Means if Newton's Method entered an infinite loop use Bisection method instead
              if ((( Func3[iter3] < 0.0) && (Func3[iter3-1] > 0.0)) || ((Func3[iter3] > 0.0) && (Func3[iter3-1] < 0.0))) {
              // Bisection Method:
                  iter_Mid = 0;
                  do {
                     iter_Mid = iter_Mid + 1;

                     if (iter_Mid == 1) {
                        Func_1 = Func3[iter3];
                        Func_2 = Func3[iter3-1];
                        Sol_1 = Solution3[iter3];
                        Sol_2 = Solution3[iter3-1];
                     }
                     if (iter_Mid > 1) MidPoint = Ts_bare_new;


          //Assuming that the sky emissivity is equal to the bare soil emissivity
                     Q_IR_sky_bare_s = epsilong * Sigma * (Surface(SurfNum).ViewFactorSky * pow_4(SkyTempKelvin) - pow_4(MidPoint) - ( 1 - epsilong ) * Surface(SurfNum).ViewFactorSky * pow_4(SkyTempKelvin) );
                     Qconv_bare_s = h_conv_bare( SurfNum, Tak, MidPoint, WS, k_air ) * (MidPoint - Tak);
					 r_a_bare = Rhoa * Cp_air * std::pow(Le_num,2/3) / h_conv_bare( SurfNum, Tak, MidPoint, WS, k_air );
					 Q_E_bare_s = Rhoa * Cp_air / gamma_s( MidPoint, Cp_air, Pa ) * ( e_s(MidPoint) - eair ) / ( r_s_sub + r_a_bare );
					 Qcond_bare_s = -Qsoilpart1 + Qsoilpart2 * ( sigma_f * ( T_soil - KelvinConv ) + ( 1 - sigma_f ) * ( MidPoint - KelvinConv ));
					 Func_MidPoint = Q_sol_abs_bare_soil + Q_IR_sky_bare_s - Qconv_bare_s - Q_E_bare_s - Qcond_bare_s;
					 if (((Func_MidPoint < 0.0) && (Func_1 > 0.0)) || ((Func_MidPoint > 0.0) && (Func_1 < 0.0))) {
							Ts_bare_new = 0.5 * ( MidPoint + Sol_1 );
							Func_2 = Func_MidPoint;
							Sol_2 = MidPoint;
					 }
					 else if (((Func_MidPoint < 0.0) && (Func_2 > 0.0)) || ((Func_MidPoint > 0.0) && (Func_2 < 0.0))) {
								Ts_bare_new = 0.5 * ( MidPoint + Sol_2 );
								Func_1 = Func_MidPoint;
								Sol_1 = MidPoint;
					 }
				  }while( abs(Ts_bare_new - MidPoint) > 0.0001 );
                  goto Label3300;
              }
			  else {
              goto Label3300;
              }
           }
        }while ( abs(Ts_bare_new - Ts_bare_old ) > 0.0001 );

        Label3300:;
		T_bare_soil = Ts_bare_new;
        r_a_bare = Rhoa * Cp_air * std::pow(Le_num,2/3) / h_conv_bare( SurfNum, Tak, T_bare_soil, WS, k_air );
        Q_E_bare_s = Rhoa * Cp_air / gamma_s( T_bare_soil, Cp_air, Pa ) * ( e_s(T_bare_soil) - eair ) / ( r_s_sub + r_a_bare );
        Q_E_bare_s_Rep = Rhoa * Cp_air / gamma_s( T_bare_soil, Cp_air, Pa ) * ( e_s(T_bare_soil) - eair ) / ( r_s_sub + r_a_bare );
        Qconv_bare_s_Rep = h_conv_bare( SurfNum, Tak, T_bare_soil, WS, k_air ) * ( T_bare_soil - Tak );
        Q_sol_bare_s_Rep = ( 1 - Alphag ) * RS;
        Q_IR_bare_s_Rep = epsilong * Sigma * ( Surface(SurfNum).ViewFactorSky * pow_4(SkyTempKelvin) - pow_4(T_bare_soil) - ( 1 - epsilong ) * Surface(SurfNum).ViewFactorSky * pow_4(SkyTempKelvin) );
    }

//############################################################################
//---Average soil temperature
        Tsoil_avg = sigma_f * T_soil + ( 1 - sigma_f ) * T_bare_soil;	
		
//---From EcoRoof
//Note: 'Q_ET_p' & 'i_fg_p' are used in places of 'Lf' & 'Lef' and are coming from equations of GreenRoof (Not EcoRoof)
//      It is the same for the soil parameters.
//Note: Since 'Q_ET_p' and 'Q_E_s' have reverse signs than those in the EcoRoof model, '-1.0*' is omitted from the Vfluxf/g
//      equations of EcoRoof.

        i_fg_p = ( -2.3793 * ( T_plant - KelvinConv ) + 2501.1 ) * 1000;  //[j/kg]
    //Check to see if ice is sublimating or frost is forming.
        if ((T_plant - KelvinConv) < 0.0 ) i_fg_p = 2.838e6;  // per FASST documentation p.15 after eqn. 37.

        i_fg_g = ( -2.3793 * ( Tsoil_avg  - KelvinConv ) + 2501.1 ) * 1000;  //[j/kg]
    //Check to see if ice is sublimating or frost is forming.
        if ((Tsoil_avg  - KelvinConv) < 0.0 ) i_fg_g = 2.838e6;  // per FASST documentation p.15 after eqn. 37.

     if (sigma_f == 0.0) {
         Vfluxf = 0.0;
     }
	 else {
         Vfluxf= Q_ET_p / i_fg_p / 990.0;               // water evapotranspire rate [m/s]
     }
     Q_E_avg = sigma_f * Q_E_s + ( 1 - sigma_f ) * Q_E_bare_s;
     Vfluxg = Q_E_avg / i_fg_g / 990.0;               // water evapotranspire rate [m/s]
     if (Vfluxf < 0.0) Vfluxf = 0.0;       // According to FASST Veg. Models p. 11, eqn 26-27, if Qfsat > qaf the actual
     if (Vfluxg < 0.0) Vfluxg = 0.0;       // evaporative fluxes should be set to zero (delta_c = 1 or 0).

    } 		
  
  //###############################################

     TempExt = Tsoil_avg - KelvinConv;
     TH( SurfNum, 1, 1 ) = Tsoil_avg - KelvinConv;

     Tsoil_avg_Rep = Tsoil_avg - KelvinConv;
     Qconv_s_avg_Rep = sigma_f * Qconv_s_Rep + ( 1 - sigma_f ) * Qconv_bare_s_Rep;
     Q_E_avg_Rep = sigma_f * Q_E_s_Rep + ( 1 - sigma_f ) * Q_E_bare_s_Rep;
     Q_sol_s_avg_Rep = sigma_f * Q_sol_soil_Rep + ( 1 - sigma_f ) * Q_sol_bare_s_Rep;
     Q_IR_s_avg_Rep = sigma_f * Q_IR_s_Rep + ( 1 - sigma_f ) * Q_IR_bare_s_Rep;
     Qcond_avg_Rep = -Qsoilpart1 + Qsoilpart2 * ( sigma_f * ( T_soil - KelvinConv ) + ( 1 - sigma_f ) * ( T_bare_soil - KelvinConv ));

     if (sigma_f != 0.0) {
         T_plant_Rep = T_plant - KelvinConv;
         Qconv_p_Rep = Qconv_p_Rep;
         Q_ET_p_Rep = Q_ET_p_Rep;
     }
	 else {
         T_plant_Rep = 0.0;
         Qconv_p_Rep = 0.0;
         Q_ET_p_Rep  = 0.0;
     }


    }
	
	
//--------------------All Functions (Beginning)---------------------------
//---Convective heat transfer coefficient for plants (h_conv)
    Real64 
	h_conv(
    //IMPLICIT NONE
      int const SurfNum,
      Real64 const Tair_k,
      Real64 const plant_temp,
      Real64 const WindSpeed,
      Real64 const k_air1
	  ) 
	  {
	  Real64 h_conv;
      Real64 const nu_air(15.66e-6);     // kinematic viscosity (m**2/s) for air at 300 K (Mills 1999 Heat Transfer)
      Real64 const Pr_air(0.71);       // Prandtl number for air
      Real64 Re;         //Reynolds number
      Real64 Gr;         //Grashof number
      Real64 Pr;         //Prandtl number
      Real64 Tavg;       //Average air temperature within the plant canopy
      Real64 Beta;       //Volumetric thermal expansion coefficient (assuming ideal gas)
      Real64 L_cha;      //Characteristic length based on the green roof dimensions
      Real64 Nu;         //Nusselt number
      Real64 length;
      Real64 wide;
      Real64 Norm;
      Real64 Lmixed;

      length = sqrt(Surface(SurfNum).Area);
      wide = length;
      Tavg = 0.5 * ( Tair_k + plant_temp );
      Beta = 1.0 / Tavg;
      L_cha = length * wide/( 2 * length + 2 * wide);

      Gr = abs(9.81 * Beta * ( plant_temp - Tair_k ) * pow_3(L_cha)/pow_2(nu_air) );
      Re = WindSpeed * length/ nu_air;
     
   //Forced convection
      if(Gr < 0.068 * std::pow(Re,2.2)) {
         Nu = 3.0 + 1.25 * 0.0253 * std::pow(Re,0.8);
         h_conv = 3.0 * Nu * k_air1/length;
      }
   
   //Mixed convection
      if(Gr > 0.068 * std::pow(Re,2.2) && Gr < 55.3 * std::pow(Re,5/3)) {
         Nu = 2.7 * std::pow( Gr/std::pow(Re,2.2),1/3) * ( 3 * 15/4 + 0.0253 * 15/16 * std::pow(Re,0.8));
         Norm = ( Gr / std::pow(Re,5/3)) / 60;
         Lmixed = ( L_cha * Norm + length * (1-Norm));
         h_conv = 3 * Nu * k_air1/Lmixed;
       }
   
   //Natural convection
      if(Gr > 55.3 * std::pow(Re,5/3)) {
         Nu = 0.15 * std::pow((Gr * Pr_air),1/3);
         h_conv = 3 * Nu * k_air1/L_cha;
       }

    return h_conv;
    }


//---Convective heat transfer coefficient for bare soil
    Real64 
	h_conv_bare(
    //IMPLICIT NONE
      int const SurfNum,
      Real64 const Tair_k,
      Real64 const BareSoil_temp,
      Real64 const WindSpeed,
      Real64 const k_air1
	  ) 
	  {
	  Real64 h_conv_bare;
      Real64 const nu_air(15.66e-6);     // kinematic viscosity (m**2/s) for air at 300 K (Mills 1999 Heat Transfer)
      Real64 const Pr_air(0.71);       // Prandtl number for air
      Real64 Re;         //Reynolds number
      Real64 Gr_soil;    //Grashof number
      Real64 Pr;         //Prandtl number
      Real64 Tavg;       //Average air temperature within the plant canopy
      Real64 Beta;       //Volumetric thermal expansion coefficient (assuming ideal gas)
      Real64 L_cha;      //Characteristic length based on the green roof dimensions
      Real64 Nu;         //Nusselt number
      Real64 length;
      Real64 wide;
      Real64 Norm;
      Real64 Lmixed;

      length = sqrt(Surface(SurfNum).Area);
      wide = length;
      Tavg = 0.5 * ( Tair_k + BareSoil_temp );
      Beta = 1.0 / Tavg;
      L_cha = length * wide/(2*length+2*wide);

      Gr_soil = abs( 9.81 * Beta * ( BareSoil_temp - Tair_k ) * pow_3(L_cha)/pow_2(nu_air) );
      Re = WindSpeed * length/nu_air;

   //Forced convection
      if(Gr_soil < 0.068 * std::pow(Re,2.2)) {
         Nu = 3.0 + 1.25 * 0.0253 * std::pow(Re,0.8);
         h_conv_bare = 2.1 * Nu * k_air1/length;
      }

   //Mixed convection
      if(Gr_soil > 0.068 * std::pow(Re,2.2) && Gr_soil < 55.3 * std::pow(Re,5/3)) {
         Nu = 2.7 * std::pow((Gr_soil/std::pow(Re,2.2)),1/3) * ( 3 * 15/4 + 0.0253 * 15/16 * std::pow(Re,0.8));
         Norm = (Gr_soil/std::pow(Re,5/3))/60;
         Lmixed = ( L_cha * Norm + length * (1-Norm));
         h_conv_bare = 2.1 * Nu * k_air1/Lmixed;
       }

   //Natural convection
      if(Gr_soil > 55.3 * std::pow(Re,5/3)) {
         Nu = 0.15 * std::pow(Gr_soil*Pr_air,1/3);
         h_conv_bare = 2.1 * Nu * k_air1/L_cha;
       }

      return h_conv_bare;
	}  


//---Saturation vapor pressure (kPa)
    Real64 e_s(
    //IMPLICIT NONE
      Real64 const Temperature
	  ) {
	  Real64 e_s;
      e_s = 0.6108 * std::exp((17.27 * (Temperature - KelvinConv)) / ((Temperature - KelvinConv) + 237.3));
    
    return e_s;
	}
   

//---f_VPD    
    Real64 f_Hum(
    //IMPLICIT NONE
      Real64 const Temperature,
      Real64 const eair
	  ) 
	  {
	  Real64 f_Hum;
      Real64 VPD_plants;
      Real64 f_VPD;
    
      VPD_plants = e_s(Temperature) - eair;
      if (VPD_plants > 0.0) {
          f_VPD = 1.0-0.41 * log(VPD_plants);
      }    
	  else {
          f_VPD = 1.0;
      }
	  
      if (f_VPD > 1.0) {
          f_VPD = 1.0;
      }
	  
      if (f_VPD < 0.0) {
          f_VPD = 0.05;
      }
	  
      f_Hum = 1.0/f_VPD;
      return f_Hum;
    }
    
//---f_temp    
    Real64 f_temp(
    //IMPLICIT NONE
      Real64 const Temperature
	  ) 
	  {
	Real64 f_temp;      
	f_temp = abs(1/(1-0.0016 * pow_2(35-(Temperature - KelvinConv))));
    return f_temp;
	}


//---Gamma: psychrometric constant
    Real64 gamma_s(
    //IMPLICIT NONE
      Real64 const Temperature,
      Real64 const Cp_air,
      Real64 const Pa
	  ) 
	  {
	  Real64 gamma_s;
      Real64 i_fg;

      i_fg = (-2.3793 * (Temperature - KelvinConv) + 2501.1) * 1000;   //Latent heat of vaporization (j/kg); 1000 is 
                                                                       //for converting kj to j.
      gamma_s = Cp_air*(Pa/1000)/(0.622*i_fg); 
    
    return gamma_s;
    }
//--------------------All Functions (End)---------------------------	

//----------------------------------------------------------	
void
	CalcEcoRoof(
		int const SurfNum, // Indicator of Surface Number for the current surface
		int const ZoneNum, // Indicator for zone number where the current surface
		int & ConstrNum, // Indicator for contruction index for the current surface
		Real64 & TempExt // Exterior temperature boundary condidtion
	)
	{
		// SUBROUTINE INFORMATION
		//     AUTHOR          David Sailor and Toan Pham
		//     DATE WRITTEN    January 2007
		//     MODIFIED        David Sailor - to fix initialization between DD runs and during warm-up
		//     RE-ENGINEERED   na

		// PURPOSE OF THIS MODULE:

		// To calculate the heat balance for surfaces with eco roof specified as outside surface
		// Note that only ONE ecoroof construction can be employed at present time. If multiple
		// surfaces have ecoroof as the outside layer the energy balance is only calculated for
		// the first such surface.

		// METHODOLOGY EMPLOYED:
		// Vikram Madhusudan's Portland State Univ. MS Thesis (Dec 2005) based on FASST model
		// of Frankenstein and Koenig (2004) - DRDC/CRREL Technical Report TR-04-25.
		// Some data used herein are from: European Centre for Medium-Range Weather Forecasts (ECMWF)
		// IFS Documentation, CY25R1 (April 2002), www.ecmwf.int/research/ifsdocs/CY24r1/Physics/
		// Physics-08-03.html.
		// The Atmospheric Boundary Layer - by J.R. Garratt (Cambridge Atmos. & Space Science Series), 316pp.
		// Using/Aliasing
		using namespace DataGlobals;
		using namespace DataEnvironment;
		using namespace DataHeatBalFanSys;
		using namespace DataHeatBalance;
		using namespace DataHeatBalSurface;
		using namespace DataSurfaces;
		//  USE DataDaylightingDevices
		//  USE DaylightingDevices,        ONLY: FindTDDPipe
		using namespace Psychrometrics;
		using ConvectionCoefficients::InitExteriorConvectionCoeff;
		using ConvectionCoefficients::SetExtConvectionCoeff;
		using ConvectionCoefficients::SetIntConvectionCoeff;

		// Locals
		//SUBROUTINE ARGUMENT DEFINITIONS:

		//SUBROUTINE PARAMETER DEFINITIONS:
		Real64 const Kv( 0.4 ); // Von Karmen's constant (source FASST)
		Real64 const rch( 0.63 ); // Turbulent Schimdt Number
		Real64 const Ks( 0.2 ); // W/m.k. Thermal Conductivity of soil
		Real64 const rche( 0.71 ); // Turbulent Prandtl Number
		Real64 const Rv( 461.53 ); // Gas Constant of Water Vapor J/kg K
		Real64 const Rair( 0.286e3 ); // Gas Constant of air J/Kg K
		Real64 const g1( 9.81 ); // Gravity. In m/sec^2.
		Real64 const Sigma( 5.6697e-08 ); // Stefan-Boltzmann constant W/m^2K^4
		Real64 const Cpa( 1005.6 ); // Specific heat of Water Vapor. (J/Kg.K)

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		static int FirstEcoSurf( 0 ); // Indicates lowest numbered surface that is an ecoroof
		// used to determine WHEN to updatesoilProps...
		int EcoLoop; // an integer loop variable for the simultaneous solution iteration

		Real64 AbsThermSurf; // Thermal absoptance of the exterior surface
		int RoughSurf; // Roughness index of the exterior (ecoroof) surface.
		Real64 HMovInsul; // "Convection" coefficient of movable insulation
		//  REAL(r64)    :: HSky                ! "Convection" coefficient from sky to surface
		//  REAL(r64)    :: HAir                ! "Convection" coefficient from air to surface (radiation)
		//  INTEGER :: OPtr
		//  INTEGER :: OSCScheduleIndex    ! Index number for OSC ConstTempSurfaceName

		static bool QuickConductionSurf( false ); // indicator for quick conduction surface
		static Real64 LAI( 0.2 ); // Leaf area index
		static Real64 epsilonf( 0.95 ); // Leaf Emisivity
		static Real64 epsilong( 0.95 ); // Soil Emisivity
		static Real64 Alphag( 0.3 ); // Ground Albedo
		static Real64 Alphaf( 0.2 ); // Leaf Albedo (reflectivity to solar radiation)
		static Real64 e0( 2.0 ); // Windless lower limit of exchange coefficient (from FASST docs)
		static Real64 RH( 50.0 ); // Relative humidity (%)
		static Real64 Pa( 101325.0 ); // Atmospheric Pressure (PA)
		static Real64 Tg( 10.0 ); // Ground Surface temperature C ***** FROM PREVIOUS TIME STEP
		static Real64 Tf( 10.0 ); // Leaf temperature C ***** FROM PREVIOUS TIME STEP
		Real64 Tgk; // Ground temperature in Kelvin
		static Real64 Zf( 0.2 ); // Height of plants (m)
		// DJS Oct 2007 release - note I got rid of the initialization of moisture and meanrootmoisture here as these
		// values are now set at beginning of each new DD and each new warm-up loop.
		// DJS
		static Real64 Moisture; // m^3/m^3.The moisture content in the soil is the value provided by a user
		static Real64 MoistureResidual( 0.05 ); // m^3/m^3. Residual & maximum water contents are unique to each material.
		// See Frankenstein et al (2004b) for data.
		static Real64 MoistureMax( 0.5 ); // Maximum volumetric moisture content (porosity) m^3/m^3
		static Real64 MeanRootMoisture; // Mean value of root moisture m^3/m^3
		static Real64 SoilThickness( 0.2 ); // Soil thickness (m)
		static Real64 StomatalResistanceMin; // s/m . ! Minimum stomatal resistance is unique for each veg. type.
		static Real64 f3( 1.0 ); // As the value of gd for tall grass is 0, then f3 = 1
		// ECMWF 2002 CY25R1 report has gd=0.0 for all veg except trees where gd=0.03.

		Real64 Ta; // current air temperature
		static Real64 Zog( 0.001 ); // Ground roughness length scale (m)
		static Real64 Za( 2.0 ); // Instrument height where atmospheric wind speed is measured (m)
		Real64 Ws; // Wind Speed (m/s)
		Real64 Waf; // Windspeed within canopy (m/s)

		Real64 Latm; // Long Wave Radiation (W/m^2)
		Real64 qaf; // mixing ratio of air near canopy

		Real64 qg; // mixing ratio of air at surface.
		static Real64 Lf; // latent heat flux
		static Real64 Vfluxf( 0.0 ); // Water evapotr. rate associated with latent heat from vegetation [m/s]
		Real64 RS; // shortwave radiation
		static Real64 Qsoil( 0.0 ); // heat flux from the soil layer

		Real64 EpsilonOne;
		//unused1208  REAL(r64) :: e
		Real64 eair;
		Real64 Rhoa;
		Real64 Tak;
		Real64 qa; // mixing ratio of air
		Real64 Tafk;
		Real64 Taf;
		Real64 Rhof;
		Real64 Rhoaf; // Average air density
		Real64 sigmaf;
		Real64 Zd; // zero displacement height (m)
		Real64 Zo; // foliage roughness length (m)
		Real64 Cfhn; // transfer coefficient at near-neutral conditions
		Real64 Cf; // bulk Transfer coefficient, equation 10 page 6 (FASST).
		static Real64 sheatf; // sensible heat flux coeff for foliage (W/m^2K)
		static Real64 sensiblef; // sensible heat transfer TO foliage (W/m^2) DJS Jan 2011
		Real64 ra; // Aerodynamic Resistance

		Real64 f1inv; // intermediate calculation variable
		Real64 f2inv; // intermediate calculation variable

		Real64 f1; // intermediate calculation variable
		Real64 f2; // intermediate calculation variable
		Real64 r_s; // Minimum Stomatal Resistance, specific to each plant
		Real64 Mg; // Surface soil moisture content m^3/m^3 (Moisture / MoistureMax)
		Real64 dOne; // intermediate calculation variable
		Real64 esf; // the saturation vapor pressure (Pa)
		Real64 qsf; // Saturation specific humidity at leaf temperature (qfsat)
		Real64 Lef; // Latent heat of vaporation at leaf surface temperature (J/kg)

		Real64 Desf; // Derivative of Saturation vapor pressure
		Real64 dqf; // Derivative of saturation specific humidity
		Real64 dqg; // this is given by Clausius-Clapeyron equation
		Real64 esg; // Saturation vapor pressure (Pa)
		Real64 qsg; // Saturation specific humidity(mixing ratio?) at ground surface temperature
		Real64 Leg; // Latent heat vaporization  at the ground temperature (J/kg)
		Real64 Desg; // derivative of esg Saturation vapor pressure(?)
		Real64 F1temp; // intermediate variable in computing flux from the soil layer
		Real64 P1; // intermediate variable in the equation for Tf
		Real64 P2; // intermediate variable in the equation for Tf and Tg
		Real64 P3; // intermediate variable in the equation for Tf and Tg
		Real64 Rhog; // Density of air at the soil surface temperature
		Real64 Rhoag; // Average density of air with respect to ground surface and air temperature
		Real64 Rib; // Richardson Number
		Real64 Chng; // bulk transfer coefficient near ground
		Real64 Ce; // bulk transfer coefficient (this is in fact Ceg in equation 28 main report)
		Real64 Gammah; // latent heat exchange stability correction factor
		Real64 Chg; // in fact it is the same as Ce (=Ceg) is transfer coefficient (but wot?)
		static Real64 sheatg; // intermediate calculation variable - sensible flux coef (W/m^2K for ground)
		static Real64 sensibleg; // sensible heat flux TO ground (w/m^2) DJS Jan 2011
		Real64 T3G; // intermediate variable in the equation for Tg
		Real64 T2G; // intermediate variable in the equation for Tg
		Real64 LeafTK; // the current leaf's temperature (Kelvin)
		Real64 SoilTK; // the current soil's temperature (Kelvin)
		Real64 Chne; // is similar to near ground bulk transfer coefficient for latent heat flux (at neutral condition)
		Real64 Tif; // previous leaf temperature
		Real64 rn; // rn is the combined effect of both stomatal and aerodynamic resistances
		// in fact this is called r'' in the main report
		static Real64 Lg( 0.0 ); // latent heat flux from ground surface
		static Real64 Vfluxg( 0.0 ); // Water evapotr. rate associated with latent heat from ground surface [m/s]
		Real64 T1G; // intermediate variable in the equation for Tg
		Real64 Qsoilpart1; // intermediate variable for evaluating Qsoil (part without the unknown)
		Real64 Qsoilpart2; // intermediate variable for evaluating Qsoil (part coeff of the ground temperature)

		//  INTEGER,EXTERNAL :: GetNewUnitNumber ! external function to return a new (unique) unit for ecoroof writing
		static int unit( 0 );
		static bool MyEnvrnFlag( true );

		Ws = WindSpeedAt( Surface( SurfNum ).Centroid.z ); // use windspeed at Z of roof
		if ( Ws < 2.0 ) { // Later we need to adjust for building roof height...
			Ws = 2.0; // Set minimum possible wind speed outside vegetation to 2.0 m/s
			// consistent with FASST TR-04-25 p. x (W' = 2.0)
		}

		if ( SurfaceWindow( SurfNum ).StormWinFlag == 1 ) ConstrNum = Surface( SurfNum ).StormWinConstruction;
		RoughSurf = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).Roughness;
		AbsThermSurf = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).AbsorpThermal;
		HMovInsul = 0.0;

		if ( Surface( SurfNum ).ExtWind ) {
			InitExteriorConvectionCoeff( SurfNum, HMovInsul, RoughSurf, AbsThermSurf, TH( SurfNum, 1, 1 ), HcExtSurf( SurfNum ), HSkyExtSurf( SurfNum ), HGrdExtSurf( SurfNum ), HAirExtSurf( SurfNum ) );
		}

		RS = BeamSolarRad + AnisoSkyMult( SurfNum ) * DifSolarRad;

		Latm = 1.0 * Sigma * 1.0 * Surface( SurfNum ).ViewFactorGround * pow_4( GroundTempKelvin ) + 1.0 * Sigma * 1.0 * Surface( SurfNum ).ViewFactorSky * pow_4( SkyTempKelvin );

		if ( EcoRoofbeginFlag ) {
			EcoRoofbeginFlag = false;
			if ( Surface( SurfNum ).HeatTransferAlgorithm != HeatTransferModel_CTF ) ShowWarningError( "CalcEcoRoof: EcoRoof simulation but HeatBalanceAlgorithm is not ConductionTransferFunction(CTF)." " Has not been tested under other solution approaches." );
			// ONLY READ ECOROOF PROPERTIES IN THE FIRST TIME
			Zf = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).HeightOfPlants; // Plant height (m)
			LAI = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).LAI; // Leaf Area Index
			Alphag = 1.0 - Material( Construct( ConstrNum ).LayerPoint( 1 ) ).AbsorpSolar; // albedo rather than absorptivity
			Alphaf = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).Lreflectivity; // Leaf Reflectivity
			epsilonf = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).LEmissitivity; // Leaf Emisivity
			StomatalResistanceMin = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).RStomata; // Leaf min stomatal resistance
			epsilong = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).AbsorpThermal; // Soil Emisivity
			MoistureMax = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).Porosity; // Max moisture content in soil
			MoistureResidual = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).MinMoisture; // Min moisture content in soil
			Moisture = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).InitMoisture; // Initial moisture content in soil
			MeanRootMoisture = Moisture; // DJS Oct 2007 Release --> all soil at same initial moisture for Reverse DD fix

			SoilThickness = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).Thickness; // Total thickness of soil layer (m)

			//DJS - This set of statements and the corresponding write statement in the UpdateSoilProps subroutine should
			//      be removed (or commented out) prior to deployment in a working version of EnergyPlus
			//Open a unit for writing ecoroof specific data to output file (in EnergyPlus directory)
			// unit=GetNewUnitNumber()
			// open(unit,file='ecoroof.txt')

			// write(unit,*)   " ECOROOF OUTPUT REPORT TRACE - HOURLY "
			// write(unit,*)   " "
			// write(unit,91)
			// 91 FORMAT (" Day Hour Flux T_g  T_f MoistTop MoistRoot CumRain CumET CumRunoff TotalIrr Dens SpecHeat  Cond  Albedo")

			FirstEcoSurf = SurfNum; // this determines WHEN to updatesoilProps

			// DJS NOVEMBER 2010 - Make calls to SetupOutput Variable to allow for reporting of ecoroof variables

			SetupOutputVariable( "Green Roof Soil Temperature [C]", Tg, "Zone", "State", "Environment" );
			SetupOutputVariable( "Green Roof Vegetation Temperature [C]", Tf, "Zone", "State", "Environment" );
			SetupOutputVariable( "Green Roof Soil Root Moisture Ratio []", MeanRootMoisture, "Zone", "State", "Environment" );
			SetupOutputVariable( "Green Roof Soil Near Surface Moisture Ratio []", Moisture, "Zone", "State", "Environment" );
			SetupOutputVariable( "Green Roof Soil Sensible Heat Transfer Rate per Area [W/m2]", sensibleg, "Zone", "State", "Environment" );
			SetupOutputVariable( "Green Roof Vegetation Sensible Heat Transfer Rate per Area [W/m2]", sensiblef, "Zone", "State", "Environment" );
			SetupOutputVariable( "Green Roof Vegetation Moisture Transfer Rate [m/s]", Vfluxf, "Zone", "State", "Environment" );
			SetupOutputVariable( "Green Roof Soil Moisture Transfer Rate [m/s]", Vfluxg, "Zone", "State", "Environment" );
			SetupOutputVariable( "Green Roof Vegetation Latent Heat Transfer Rate per Area [W/m2]", Lf, "Zone", "State", "Environment" );
			SetupOutputVariable( "Green Roof Soil Latent Heat Transfer Rate per Area [W/m2]", Lg, "Zone", "State", "Environment" );

			SetupOutputVariable( "Green Roof Cumulative Precipitation Depth [m]", CumPrecip, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Cumulative Irrigation Depth [m]", CumIrrigation, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Cumulative Runoff Depth [m]", CumRunoff, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Cumulative Evapotranspiration Depth [m]", CumET, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Current Precipitation Depth [m]", CurrentPrecipitation, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Current Irrigation Depth [m]", CurrentIrrigation, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Current Runoff Depth [m]", CurrentRunoff, "Zone", "Sum", "Environment" );
			SetupOutputVariable( "Green Roof Current Evapotranspiration Depth [m]", CurrentET, "Zone", "Sum", "Environment" );

			// DJS NOVEMBER 2010 - end of calls to setup output of ecoroof variables

		} // Initialization statements for first entry into ecoroof routines

		// DJS July 2007
		// Make sure the ecoroof module resets its conditions at start of EVERY warmup day and every new design day
		// for Reverse DD testing

		if ( BeginEnvrnFlag || WarmupFlag ) {
			Moisture = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).InitMoisture; // Initial moisture content in soil
			MeanRootMoisture = Moisture; // Start the root zone moisture at the same value as the surface.
			Alphag = 1.0 - Material( Construct( ConstrNum ).LayerPoint( 1 ) ).AbsorpSolar; // albedo rather than absorptivity
		}
		// DJS July 2007

		if ( BeginEnvrnFlag && MyEnvrnFlag ) {
			Tgold = OutDryBulbTempAt( Surface( SurfNum ).Centroid.z ); //OutDryBulbTemp           ! initial guess
			Tfold = OutDryBulbTempAt( Surface( SurfNum ).Centroid.z ); //OutDryBulbTemp           ! initial guess
			Tg = 10.0;
			Tf = 10.0;
			Vfluxf = 0.0;
			Vfluxg = 0.0;
			CumRunoff = 0.0;
			CumET = 0.0;
			CumPrecip = 0.0;
			CumIrrigation = 0.0;
			CurrentRunoff = 0.0;
			CurrentET = 0.0;
			CurrentPrecipitation = 0.0;
			CurrentIrrigation = 0.0;
			MyEnvrnFlag = false;
		}

		if ( ! BeginEnvrnFlag ) {
			MyEnvrnFlag = true;
		}

		// If current surface is = FirstEcoSurf then for this time step we need to update the soil moisture
		if ( SurfNum == FirstEcoSurf ) {
			UpdateSoilProps( Moisture, MeanRootMoisture, MoistureMax, MoistureResidual, SoilThickness, Vfluxf, Vfluxg, ConstrNum, Alphag, unit, Tg, Tf, Qsoil );

			Ta = OutDryBulbTempAt( Surface( SurfNum ).Centroid.z ); // temperature outdoor - Surface is dry, use normal correlation
			Tg = Tgold;
			Tf = Tfold;

			if ( Construct( ConstrNum ).CTFCross( 0 ) > 0.01 ) {
				QuickConductionSurf = true;
				F1temp = Construct( ConstrNum ).CTFCross( 0 ) / ( Construct( ConstrNum ).CTFInside( 0 ) + HConvIn( SurfNum ) );
				Qsoilpart1 = -CTFConstOutPart( SurfNum ) + F1temp * ( CTFConstInPart( SurfNum ) + QRadSWInAbs( SurfNum ) + QRadThermInAbs( SurfNum ) + Construct( ConstrNum ).CTFSourceIn( 0 ) * QsrcHist( SurfNum, 1 ) + HConvIn( SurfNum ) * MAT( ZoneNum ) + NetLWRadToSurf( SurfNum ) );
			} else {
				Qsoilpart1 = -CTFConstOutPart( SurfNum ) + Construct( ConstrNum ).CTFCross( 0 ) * TempSurfIn( SurfNum );
				F1temp = 0.0;
			}

			Qsoilpart2 = Construct( ConstrNum ).CTFOutside( 0 ) - F1temp * Construct( ConstrNum ).CTFCross( 0 );

			Pa = StdBaroPress; // standard atmospheric pressure (apparently in Pascals)
			Tgk = Tg + KelvinConv;
			Tak = Ta + KelvinConv;

			sigmaf = 0.9 - 0.7 * std::exp( -0.75 * LAI ); // Fractional veg cover based on (2) from FASST TR-04-25
			// Formula for grasses modified to incorporate limits from
			// Table 1 for sigmaf_max and min (0.20 to 0.9)

			EpsilonOne = epsilonf + epsilong - epsilong * epsilonf; // Checked (eqn. 6 in FASST Veg Models)
			RH = OutRelHum; // Get humidity in % from the DataEnvironment.cc
			eair = ( RH / 100.0 ) * 611.2 * std::exp( 17.67 * Ta / ( Tak - 29.65 ) );
			qa = ( 0.622 * eair ) / ( Pa - 1.000 * eair ); // Mixing Ratio of air
			Rhoa = Pa / ( Rair * Tak ); // Density of air. kg/m^3
			Tif = Tf;

			// Air Temperature within the canopy is given as
			// (Deardorff (1987)). Kelvin. based of the previous temperatures
			Tafk = ( 1.0 - sigmaf ) * Tak + sigmaf * ( 0.3 * Tak + 0.6 * ( Tif + KelvinConv ) + 0.1 * Tgk );

			Taf = Tafk - KelvinConv; // Air Temperature within canopy in Celcius (C).
			Rhof = Pa / ( Rair * Tafk ); // Density of air at the leaf temperature
			Rhoaf = ( Rhoa + Rhof ) / 2.0; // Average of air density
			Zd = 0.701 * std::pow( Zf, 0.979 ); // Zero displacement height
			Zo = 0.131 * std::pow( Zf, 0.997 ); // Foliage roughness length. (m) Source Ballick (1981)
			if ( Zo < 0.02 ) Zo = 0.02; // limit based on p.7 TR-04-25 and Table 2

			//transfer coefficient at near-neutral condition Cfhn
			Cfhn = pow_2( Kv / std::log( ( Za - Zd ) / Zo ) ); //Equation 12, page 7, FASST Model
			Waf = 0.83 * std::sqrt( Cfhn ) * sigmaf * Ws + ( 1.0 - sigmaf ) * Ws; // Wind Speed inside foliage. Equation #6, FASST model
			Cf = 0.01 * ( 1.0 + 0.3 / Waf ); // The bulk Transfer coefficient, equation 10 page 6.
			sheatf = e0 + 1.1 * LAI * Rhoaf * Cpa * Cf * Waf; // Intermediate calculation for Sensible Heat Transfer
			sensiblef = sheatf * ( Taf - Tf ); // DJS Jan 2011 sensible flux TO foliage into air (Frankenstein 2004, eqn7)
			//sourced from Frankenstein et al (2004a). Added e0 windless correction factor.
			//an early version had (1.0-0.7)*sigmaf in calc of sensiblef... how did that get there!?! Fixed.

			//These parameters were taken from "The Atm Boundary Layer", By J.R. Garratt
			//NOTE the Garratt eqn. (A21) gives esf in units of hPA so we have multiplied
			//the constant 6.112 by a factor of 100.
			esf = 611.2 * std::exp( 17.67 * Tif / ( Tif + KelvinConv - 29.65 ) );

			// From Garratt - eqn. A21, p284. Note that Tif and Tif+KelvinConv usage is correct.
			// Saturation specific humidity at leaf temperature again based on previous temperatures

			qsf = 0.622 * esf / ( Pa - 1.000 * esf ); // "The Atm Boundary Layer", J.R Garrat for Saturation mixing ratio
			// Calculate stomatal resistance and atmospheric resistance Part
			ra = 1.0 / ( Cf * Waf ); // Aerodynamic Resistance. Resistance that is caused
			// by the boundary layer on a leaf surface to transfer water vapor. It is measured in
			// s/m and depends on wind speed, leaf's surface roughness,
			// and stability of atsmophere.

			f1inv = min( 1.0, ( 0.004 * RS + 0.005 ) / ( 0.81 * ( 0.004 * RS + 1.0 ) ) ); // SW radiation-related term
			f1 = 1.0 / f1inv;
			if ( MoistureMax == MoistureResidual ) {
				f2inv = 1.0e10;
			} else {
				f2inv = ( MeanRootMoisture - MoistureResidual ) / ( MoistureMax - MoistureResidual ); //Equation 19 p. 9 FASST model
			}

			//In FASST, Eq 20 is used to compute Moisture.

			f2 = 1.0 / f2inv; // In dry areas f2 --> LARGE so that r_s --> LARGE
			// and both rn and Latent flux --> 0.0
			f3 = 1.0 / ( std::exp( -0.0 * ( esf - eair ) ) ); // Note the 0.0 here is gd which has value of 0.0
			// for most plants and 0.03 for trees (see ECMWF
			// note given above.
			r_s = StomatalResistanceMin * f1 * f2 * f3 / LAI; //  Stomatal Resistance (r_s)
			rn = ra / ( ra + r_s ); //rn is foliage surface wetness ... NOT a resistance

			// This routine is to calculate ground moisture factor. This factor is from *****
			Mg = Moisture / MoistureMax; // m^3/m^3.
			dOne = 1.0 - sigmaf * ( 0.6 * ( 1.0 - rn ) + 0.1 * ( 1.0 - Mg ) );

			//Latent heat of vaporation at leaf surface temperature. The source of this
			//equation is Henderson-Sellers (1984)
			Lef = 1.91846e6 * pow_2( ( Tif + KelvinConv ) / ( Tif + KelvinConv - 33.91 ) );
			//Check to see if ice is sublimating or frost is forming.
			if ( Tfold < 0.0 ) Lef = 2.838e6; // per FASST documentation p.15 after eqn. 37.

			//Derivative of Saturation vapor pressure, which is used in the calculation of
			//derivative of saturation specific humidity.

			Desf = 611.2 * std::exp( 17.67 * ( Tf / ( Tf + KelvinConv - 29.65 ) ) ) * ( 17.67 * Tf * ( -1.0 ) * std::pow( Tf + KelvinConv - 29.65, -2 ) + 17.67 / ( KelvinConv - 29.65 + Tf ) );
			dqf = ( ( 0.622 * Pa ) / pow_2( Pa - esf ) ) * Desf; //Derivative of saturation specific humidity
			esg = 611.2 * std::exp( 17.67 * ( Tg / ( ( Tg + KelvinConv ) - 29.65 ) ) ); //Pa saturation vapor pressure
			// From Garratt - eqn. A21, p284.
			// Note that Tg and Tg+KelvinConv usage is correct.
			qsg = 0.622 * esg / ( Pa - esg ); //Saturation mixing ratio at ground surface temperature.

			//Latent heat vaporization  at the ground temperature
			Leg = 1.91846e6 * pow_2( Tgk / ( Tgk - 33.91 ) );
			//Check to see if ice is sublimating or frost is forming.
			if ( Tgold < 0.0 ) Leg = 2.838e6; // per FASST documentation p.15 after eqn. 37.

			Desg = 611.2 * std::exp( 17.67 * ( Tg / ( Tg + KelvinConv - 29.65 ) ) ) * ( 17.67 * Tg * ( -1.0 ) * std::pow( Tg + KelvinConv - 29.65, -2 ) + 17.67 / ( KelvinConv - 29.65 + Tg ) );
			dqg = ( 0.622 * Pa / pow_2( Pa - esg ) ) * Desg;

			//Final Ground Atmosphere Energy Balance
			//Density of air at the soil surface temperature
			Rhog = Pa / ( Rair * Tgk );

			//Average density of air with respect to ground surface and air temperature
			Rhoag = ( Rhoa + Rhog ) / 2.0;
			Rib = 2.0 * g1 * Za * ( Taf - Tg ) / ( ( Tafk + Tgk ) * pow_2( Waf ) ); //Richardson Number

			// Compute the stability factor Gammah
			if ( Rib < 0.0 ) {
				Gammah = std::pow( 1.0 - 16.0 * Rib, -0.5 );
			} else {
				if ( Rib >= 0.19 ) {
					Rib = 0.19;
				}
				Gammah = std::pow( 1.0 - 5.0 * Rib, -0.5 );
			}

			if ( RoughSurf == VerySmooth ) { //  6= very smooth, 5=smooth, 4= med. sm. ,3= med. rough. , 2= rough, 1= Very rough
				Zog = 0.0008;
			} else if ( RoughSurf == Smooth ) {
				Zog = 0.0010;
			} else if ( RoughSurf == MediumSmooth ) {
				Zog = 0.0015;
			} else if ( RoughSurf == MediumRough ) {
				Zog = 0.0020;
			} else if ( RoughSurf == Rough ) {
				Zog = 0.0030;
			} else { // VeryRough
				Zog = 0.005;
			}

			Chng = pow_2( Kv / std::log( Za / Zog ) ) / rch; // bulk transfer coefficient near ground
			Chg = Gammah * ( ( 1.0 - sigmaf ) * Chng + sigmaf * Cfhn );
			sheatg = e0 + Rhoag * Cpa * Chg * Waf; // added the e0 windless correction
			sensibleg = sheatg * ( Taf - Tg ); // sensible flux TO soil (W/m^2) DJS Jan 2011 (eqn. 32 in Frankenstein 2004)

			Chne = pow_2( Kv / std::log( Za / Zog ) ) / rche;
			Ce = Gammah * ( ( 1.0 - sigmaf ) * Chne + sigmaf * Cfhn ); // this is in fact Ceg in eq (28)

			//we can approximate Gammae by Gammah (Supported by FASST Veg Models p. 15)
			qaf = ( ( 1.0 - sigmaf ) * qa + sigmaf * ( 0.3 * qa + 0.6 * qsf * rn + 0.1 * qsg * Mg ) ) / ( 1.0 - sigmaf * ( 0.6 * ( 1.0 - rn ) + 0.1 * ( 1.0 - Mg ) ) );
			qg = Mg * qsg + ( 1.0 - Mg ) * qaf; //eq main report (13)
			// According to FASST documentation this is correct.
			// The following also used Rhoaf and Rhoag respectively... shouldn't these be water densities??!!
			Lf = Lef * LAI * Rhoaf * Cf * Waf * rn * ( qaf - qsf ); // This had Leg but should be Lef...
			Lg = Ce * Leg * Waf * Rhoag * ( qaf - qg ) * Mg; // In the FASST documentation there is NO Mg. However, in looking
			// back at the Deardorff 1978 paper it appears that an alpha = Mg term is
			// used to distinguish from POTENTIAL and ACTUAL ground surface evaporation...
			// the Lf and Lg calculations are NOT used in this formulation
			// rather, the effects are included in terms of dqg and dqf !
			// These equations for Lf and Lg are based on Deardorff's paper, but there is
			// a sign difference !!! (qsf -qaf) and (qg - qaf) ?!
			// These may be useful, however for telling the UpdateSoilProps routine
			// how much evaporation has taken place...
			Vfluxf = -1.0 * Lf / Lef / 990.0; // water evapotranspire rate [m/s]
			Vfluxg = -1.0 * Lg / Leg / 990.0; // water evapotranspire rate [m/s]
			if ( Vfluxf < 0.0 ) Vfluxf = 0.0; // According to FASST Veg. Models p. 11, eqn 26-27, if Qfsat > qaf the actual
			if ( Vfluxg < 0.0 ) Vfluxg = 0.0; // evaporative fluxes should be set to zero (delta_c = 1 or 0).

			// P1, P2, P3 corespond to first, second and third terms of equation 37 in the main report.

			//   Note: the FASST model has a term -gamma_p*(1.0-exp...) in first line for P1 (c1_f) where gamma_p is
			//   a precipitation variable. So, if we assume no precip this term vanishes. We should
			//   revisit this issue later.
			//   Implement an iterative solution scheme to solve the simultaneous equations for Leaf and Soil temperature.
			//   Prior experience suggests that no more than 3 iterations are likely needed
			LeafTK = Tf + KelvinConv;
			SoilTK = Tg + KelvinConv;

			for ( EcoLoop = 1; EcoLoop <= 3; ++EcoLoop ) {
				P1 = sigmaf * ( RS * ( 1.0 - Alphaf ) + epsilonf * Latm ) - 3.0 * sigmaf * epsilonf * epsilong * Sigma * pow_4( SoilTK ) / EpsilonOne - 3.0 * ( -sigmaf * epsilonf * Sigma - sigmaf * epsilonf * epsilong * Sigma / EpsilonOne ) * pow_4( LeafTK ) + sheatf * ( 1.0 - 0.7 * sigmaf ) * ( Ta + KelvinConv ) + LAI * Rhoaf * Cf * Lef * Waf * rn * ( ( 1.0 - 0.7 * sigmaf ) / dOne ) * qa + LAI * Rhoaf * Cf * Lef * Waf * rn * ( ( ( 0.6 * sigmaf * rn ) / dOne ) - 1.0 ) * ( qsf - LeafTK * dqf ) + LAI * Rhoaf * Cf * Lef * Waf * rn * ( ( 0.1 * sigmaf * Mg ) / dOne ) * ( qsg - SoilTK * dqg );
				P2 = 4.0 * ( sigmaf * epsilonf * epsilong * Sigma ) * pow_3( SoilTK ) / EpsilonOne + 0.1 * sigmaf * sheatf + LAI * Rhoaf * Cf * Lef * Waf * rn * ( 0.1 * sigmaf * Mg ) / dOne * dqg;
				P3 = 4.0 * ( -sigmaf * epsilonf * Sigma - ( sigmaf * epsilonf * Sigma * epsilong ) / EpsilonOne ) * pow_3( LeafTK ) + ( 0.6 * sigmaf - 1.0 ) * sheatf + LAI * Rhoaf * Cf * Lef * Waf * rn * ( ( ( 0.6 * sigmaf * rn ) / dOne ) - 1.0 ) * dqf;

				//T1G, T2G, & T3G corresponds to first, second & third terms of equation 38
				//in the main report.
				//  as with the equations for vegetation the first term in the ground eqn in FASST has a
				//  term starting with gamma_p --- if no precip this vanishes. Again, revisit this issue later.

				T1G = ( 1.0 - sigmaf ) * ( RS * ( 1.0 - Alphag ) + epsilong * Latm ) - ( 3.0 * ( sigmaf * epsilonf * epsilong * Sigma ) / EpsilonOne ) * pow_4( LeafTK ) - 3.0 * ( -( 1.0 - sigmaf ) * epsilong * Sigma - sigmaf * epsilonf * epsilong * Sigma / EpsilonOne ) * pow_4( SoilTK ) + sheatg * ( 1.0 - 0.7 * sigmaf ) * ( Ta + KelvinConv ) + Rhoag * Ce * Leg * Waf * Mg * ( ( 1.0 - 0.7 * sigmaf ) / dOne ) * qa + Rhoag * Ce * Leg * Waf * Mg * ( 0.1 * sigmaf * Mg / dOne - Mg ) * ( qsg - SoilTK * dqg ) + Rhoag * Ce * Leg * Waf * Mg * ( 0.6 * sigmaf * rn / dOne ) * ( qsf - LeafTK * dqf ) + Qsoilpart1 + Qsoilpart2 * ( KelvinConv ); //finished by T1G

				T2G = 4.0 * ( -( 1.0 - sigmaf ) * epsilong * Sigma - sigmaf * epsilonf * epsilong * Sigma / EpsilonOne ) * pow_3( SoilTK ) + ( 0.1 * sigmaf - 1.0 ) * sheatg + Rhoag * Ce * Leg * Waf * Mg * ( 0.1 * sigmaf * Mg / dOne - Mg ) * dqg - Qsoilpart2;

				T3G = ( 4.0 * ( sigmaf * epsilong * epsilonf * Sigma ) / EpsilonOne ) * pow_3( LeafTK ) + 0.6 * sigmaf * sheatg + Rhoag * Ce * Leg * Waf * Mg * ( 0.6 * sigmaf * rn / dOne ) * dqf;

				LeafTK = 0.5 * ( LeafTK + ( P1 * T2G - P2 * T1G ) / ( -P3 * T2G + T3G * P2 ) ); // take avg of old and new each iteration
				SoilTK = 0.5 * ( SoilTK + ( P1 * T3G - P3 * T1G ) / ( -P2 * T3G + P3 * T2G ) ); // take avg of old and new each iteration
				// in earlier implementations we simply assigned new Leaf and Soil temps once (no iteration -- ecoloop =1,1, but
				// with LeafTK = ( P1*T2G - P2*T1G )/( -P3*T2G + T3G*P2 )  and
				// SoilTK =(SoilTK+( P1*T3G - P3*T1G )/( -P2*T3G + P3*T2G ). This iterative solution was initially attempted to
				// deal with stability issues associated with the CTF. It has virtually no impact on temperatures and it is not
				// clear how much it helped with stability problems. Eventually when CTF solution is replaced with a finite
				// difference scheme this loop structure should be removed.

			} // This loop does an iterative solution of the simultaneous equations
			Qsoil = -1.0 * ( Qsoilpart1 - Qsoilpart2 * ( SoilTK - KelvinConv ) ); // This is heat flux INTO top of the soil
			Tfold = LeafTK - KelvinConv;
			Tgold = SoilTK - KelvinConv;

		} // if firstecosurface (if not we do NOT need to recalculate ecoroof energybalance as all ecoroof surfaces MUST be the same
		// this endif was moved here from the if statement regarding whether we are looking at the first ecoroof surface or not.

		TH( SurfNum, 1, 1 ) = Tgold; // SoilTemperature
		TempExt = Tgold;

	}

	void
	UpdateSoilProps(
		Real64 & Moisture,
		Real64 & MeanRootMoisture,
		Real64 const MoistureMax,
		Real64 const MoistureResidual,
		Real64 const SoilThickness,
		Real64 const Vfluxf, // Water mass flux from vegetation [m/s]
		Real64 const Vfluxg, // Water mass flux from soil surface [m/s]
		int & ConstrNum, // Indicator for contruction index for the current surface
		Real64 & Alphag,
		int const unit, // unused1208
		Real64 const Tg, // unused1208
		Real64 const Tf, // unused1208
		Real64 const Qsoil // unused1208
	)
	{
		// SUBROUTINE INFORMATION
		//     AUTHOR          David Sailor
		//     DATE WRITTEN    Jan 2007
		//     MODIFIED        Stephen Forner, Portland State University (SF); 7/15/2010
		//     RE-ENGINEERED   na

		// PURPOSE OF THIS MODULE:

		// Track moisture input/output to ecoroof soil media (precipitation, irrigation, evapotranspiration, runoff)
		// Update soil thermal properties associated with variations in soil moisture and update CTF calculations
		// for the ecoroof construction layer.

		// METHODOLOGY EMPLOYED:
		// Define 2 soil layers (top and root). Moisture redistribution between top and root layers is proportional
		// to moisture differences. Input and Output of moisture are partitioned between layers.
		// Soil thermal properties vary based on non-dimensionalization of experimental data for 8 typical soils.
		// Specifically, THERMAL PROPERTY = Dry Value + (fraction of moisture content)*Wet Value

		// Using/Aliasing
		using namespace DataGlobals;
		using namespace DataEnvironment;
		using namespace DataSurfaces;
		using DataWater::RainFall;
		using General::RoundSigDigits;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		static Real64 const depth_fac( ( 161240.0 * std::pow( 2.0, -2.3 ) ) / 60.0 );

		//Soil Parameters from Reference listed in the code:
		Real64 const alpha( 23.0 ); // These parameters are empirical constants
		Real64 const n( 1.27 ); // These parameters are empirical constants
		Real64 const lambda( 0.5 ); // These parameters are empirical constants
		//This is another parameter of the soil which describes the soil conductivity at the saturation point (m/s)
		Real64 const SoilConductivitySaturation( 5.157e-7 );

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Real64 RatioMax;
		Real64 RatioMin;
		Real64 MoistureDiffusion; // moisture transport down from near-surface to root zone
		static Real64 TopDepth; // Thickness of "near-surface" soil layer
		static Real64 RootDepth( 0.0 ); // Thickness of "root zone" soil layer //Autodesk Was used uninitialized
		// Note TopDepth+RootDepth = thickness of ecoroof soil layer
		static Real64 SecondsPerTimeStep; // Seconds per TimeStep
		Real64 SoilConductivity; // Moisture dependent conductivity to be fed back into CTF Calculator
		Real64 SoilSpecHeat; // Moisture dependent Spec. Heat to be fed back into CTF Calculator
		Real64 SoilAbsorpSolar; // Moisture dependent Solar absorptance (1-albedo)
		Real64 SoilDensity; // Moisture dependent density to be fed back into CTF Calculator

		Real64 SatRatio;
		Real64 TestRatio; // Ratio to determine if timestep change in properties is too abrupt for CTF

		static Real64 DryCond; // Dry soil value of conductivity
		static Real64 DryDens; // Dry soil value of density
		static Real64 DryAbsorp; // Dry soil value of solar absorptance (1-albedo)
		static Real64 DrySpecHeat; // Dry soil value of specific heat
		Real64 AvgMoisture; // Average soil moisture over depth of ecoroof media

		static bool UpdatebeginFlag( true ); // one time flag

		static Real64 CapillaryPotentialTop( -3.8997 ); // This variable keeps track of the capillary potential of the soil in both layers and time (m)
		static Real64 CapillaryPotentialRoot( -3.8997 ); // This variable keeps track of the capillary potential of the soil in both layers and time (m)
		static Real64 SoilHydroConductivityTop( 8.72e-6 ); // This is the soil water conductivity in the soil (m/s)
		static Real64 SoilHydroConductivityRoot( 8.72e-6 ); // This is the soil water conductivity in the soil (m/s)
		static Real64 SoilConductivityAveTop( 8.72e-6 ); // This is the average soil water conductivity (m/s)
		static Real64 SoilConductivityAveRoot( 8.72e-6 );
		static Real64 RelativeSoilSaturationTop; // Relative Soil Saturation (soil moisture-residual soil moisture)/(saturation soil moisture-residual soil moisture)
		static Real64 RelativeSoilSaturationRoot;
		static Real64 TestMoisture( 0.15 ); // This makes sure that the moisture cannot change by too much in each step
		int index1;
		static int ErrIndex( 0 );

		// NOTE:  As Energyplus calls the energy balance manager (and hence CalcEcoroof)
		// once for each surface within each zone that has an ecoroof
		// --- the CALCECOROOF routine is called multiple times within each time step
		// So, it is important that the UpdateSoilProps subroutine is called ONLY ONCE for each time step!!!

		// Recall Moisture = near-surface moisture value (m^3/m^3)
		// Recall MeanRootMoisture = root zone moisture value (m^3/m^3)

		//DJS 2009 set the ratiomax and ratiomin values in the code now (rather than as parameters) so that
		//DJS 2009 we can link them to timesteps and make these limits apply to actual RATES...
		//DJS 2009 reasonable rates are +/- 10% change in properties per 15 minute period... Otherwise we have
		//DJS 2009 stability issues.
		//DJS 2011 FEB - Since we no longer use CTF with soil-dependent properties (Do not RECALL INITCONDUCTION...
		//DJS 2011 FEB - we may be able to get away with NO limits on rates of change when using CFD routine.
		//DJS 2011 FEB - for now we stick with 20% per quarter hour.
		RatioMax = 1.0 + 0.20 * MinutesPerTimeStep / 15.0;
		RatioMin = 1.0 - 0.20 * MinutesPerTimeStep / 15.0;

		if ( UpdatebeginFlag ) {

			// SET dry values that NEVER CHANGE
			DryCond = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).Conductivity;
			DryDens = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).Density;
			DryAbsorp = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).AbsorpSolar;
			DrySpecHeat = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).SpecHeat;

			// DETERMINE RELATIVE THICKNESS OF TWO LAYERS OF SOIL (also unchanging)
			if ( SoilThickness > 0.12 ) {
				TopDepth = 0.06; // For now use 6cm as top depth - everything else is root layer
			} else {
				TopDepth = 0.5 * SoilThickness; // In unusual case of very thin soil make topdepth half of total
			}
			//This loop outputs the minimum number of time steps needed to keep the solution stable
			//The equation is minimum timestep in seconds=161240*((number of layers)**(-2.3))*(Total thickness of the soil)**2.07
			if ( Material( Construct( ConstrNum ).LayerPoint( 1 ) ).EcoRoofCalculationMethod == 2 ) {
				Real64 const depth_limit( depth_fac * std::pow( TopDepth + RootDepth, 2.07 ) );
				for ( index1 = 1; index1 <= 20; ++index1 ) {
					if ( double( MinutesPerTimeStep / index1 ) <= depth_limit ) break; //Autodesk RootDepth was used uninitialized here
				}
				if ( index1 > 1 ) {
					ShowSevereError( "CalcEcoRoof: Too few time steps per hour for stability." );
					ShowContinueError( "...Entered Timesteps per hour=[" + RoundSigDigits( NumOfTimeStepInHour ) + "], Change to some value greater than [" + RoundSigDigits( 60 * index1 / MinutesPerTimeStep ) + "] for assured stability." );
					//      CALL ShowFatalError('Program terminates due to previous condition.')
				}
			}

			RootDepth = SoilThickness - TopDepth;
			//Next create a timestep in seconds
			SecondsPerTimeStep = MinutesPerTimeStep * 60.0;

			UpdatebeginFlag = false;
		}

		CurrentRunoff = 0.0; // Initialize current time step runoff as it is used in several spots below...

		// FIRST Subtract water evaporated by plants and at soil surface
		Moisture -= ( Vfluxg ) * MinutesPerTimeStep * 60.0 / TopDepth; // soil surface evaporation
		MeanRootMoisture -= ( Vfluxf ) * MinutesPerTimeStep * 60.0 / RootDepth; // plant extraction from root zone

		// NEXT Update evapotranspiration summary variable for print out
		CurrentET = ( Vfluxg + Vfluxf ) * MinutesPerTimeStep * 60.0; // units are meters
		if ( ! WarmupFlag ) {
			CumET += CurrentET;
		}

		// NEXT Add Precipitation to surface soil moisture variable (if a schedule exists)
		if ( ! WarmupFlag ) {
			CurrentPrecipitation = 0.0; // first initialize to zero
		}
		CurrentPrecipitation = 0.0; // first initialize to zero
		if ( RainFall.ModeID == RainSchedDesign ) {
			CurrentPrecipitation = RainFall.CurrentAmount; //  units of m
			Moisture += CurrentPrecipitation / TopDepth; // x (m) evenly put into top layer
			if ( ! WarmupFlag ) {
				CumPrecip += CurrentPrecipitation;
			}
		}

		// NEXT Add Irrigation to surface soil moisture variable (if a schedule exists)
		CurrentIrrigation = 0.0; // first initialize to zero
		Irrigation.ActualAmount = 0.0;
		if ( Irrigation.ModeID == IrrSchedDesign ) {
			CurrentIrrigation = Irrigation.ScheduledAmount; // units of m
			Irrigation.ActualAmount = CurrentIrrigation;
			//    elseif (Irrigation%ModeID ==IrrSmartSched .and. moisture .lt. 0.4d0*MoistureMax) then
		} else if ( Irrigation.ModeID == IrrSmartSched && Moisture < Irrigation.IrrigationThreshold * MoistureMax ) {
			// Smart schedule only irrigates when scheduled AND the soil is less than 40% saturated
			CurrentIrrigation = Irrigation.ScheduledAmount; // units of m
			Irrigation.ActualAmount = CurrentIrrigation;
		}

		Moisture += CurrentIrrigation / TopDepth; // irrigation in (m)/timestep put into top layer
		if ( ! WarmupFlag ) {
			CumIrrigation += CurrentIrrigation;
		}

		// Note: If soil top layer gets a massive influx of rain &/or irrigation some of
		// the water will simply run right off the top and not penetrate at all!
		// At the present time this limit is fairly small due to some minor stability issues
		// in EnergyPlus. If the moisture changes too rapidly the code cannot handle the rapid changes in
		// surface characteristics and heat fluxes. The result that I've noticed is a non-physical fluctation
		// in ground surface temperature that oscillates up to 10 deg C from one hour to the next until the
		// code catches up. The temporary solution is to simply limit how much moisture can enter the soil
		// in any time step to 0.5"/hour. In the future this might be fixed by running with finer time steps
		// or by using a finite difference temperature solution rather than the CTF.
		// I suspect that 15 minute intervals may be needed. Another option is to have an internal moisture
		// overflow bin that will hold extra moisture and then distribute it in subsequent hours. This way the
		// soil still gets the same total moisture... it is just distributed over a longer period.
		if ( CurrentIrrigation + CurrentPrecipitation > 0.5 * 0.0254 * MinutesPerTimeStep / 60.0 ) {
			CurrentRunoff = CurrentIrrigation + CurrentPrecipitation - ( 0.5 * 0.0254 * MinutesPerTimeStep / 60.0 );
			// If we get here then TOO much moisture has already been added to soil (must now subtract excess)
			Moisture -= CurrentRunoff / TopDepth; // currently any incident moisture in excess of 1/4 " per hour
			// simply runs off the top of the soil.
		}
		// Now, if top layer is beyond saturation... the excess simply runs off without penetrating into the lower
		// layers.
		if ( Moisture > MoistureMax ) {
			CurrentRunoff += ( Moisture - MoistureMax ) * TopDepth;
			Moisture = MoistureMax;
		}

		if ( Material( Construct( ConstrNum ).LayerPoint( 1 ) ).EcoRoofCalculationMethod == 1 ) {

			//THE SECTION BELOW WAS THE INITIAL MOISTURE DISTRIBUTION MODEL.
			//Any line with "!-" was code.  A line with "!" was just a comment.  This is done in case this code needs to be resurected in the future.
			//See below this commented out code for the new moisture distribution model.
			//*********************************************************************************************************
			//*********************************************************************************************************
			// NEXT Redistribute moisture based on moisture diffusion.
			// The effective diffusivities should be revisted when better moisture transport data in ecoroof soils are
			// available.
			// Here the diffusion rate is in units of [1/s]
			// A value of 0.0001 would be ~ 36% / hour
			// A value of 0.00005 would be ~ 18%/hour change in moisture
			// A value of 0.000025 gives about a 9%/hour change in moisture
			// a value of 0.0000125 gives 5%/hour...
			// Note: This formulation allows moisture to have a directional bias - ie, it can sink into soil easier than
			// it can be brought up.
			if ( Moisture > MeanRootMoisture ) {
				// move moisture from top layer into root zone
				MoistureDiffusion = min( ( MoistureMax - MeanRootMoisture ) * RootDepth, ( Moisture - MeanRootMoisture ) * TopDepth );
				MoistureDiffusion = max( 0.0, MoistureDiffusion ); // Safety net to keep positive (not needed?)
				// at this point moistureDiffusion is in units of (m)/timestep
				MoistureDiffusion *= 0.00005 * MinutesPerTimeStep * 60.0;
				Moisture -= MoistureDiffusion / TopDepth;
				MeanRootMoisture += MoistureDiffusion / RootDepth;
			} else if ( MeanRootMoisture > Moisture ) {
				// move moisture to top layer from root zone
				MoistureDiffusion = min( ( MoistureMax - Moisture ) * TopDepth, ( MeanRootMoisture - Moisture ) * RootDepth );
				MoistureDiffusion = max( 0.0, MoistureDiffusion ); // Safety net (not needed?)
				// at this point moistureDiffusion is in units of (m)/timestep
				MoistureDiffusion *= 0.00001 * MinutesPerTimeStep * 60.0;
				Moisture += MoistureDiffusion / TopDepth;
				MeanRootMoisture -= MoistureDiffusion / RootDepth;
			}
		} else {
			//********************************************************************************************************
			//********************************************************************************************************
			//THE SECTION ABOVE WAS THE MOISTURE DISTRIBUTION MODEL. REPLACED SF-7/21/2010

			//NEXT redistribute the moisture in the soil based on:
			//Marcel G Schaap and Martinus Th van Genuchten, 2006, 'A modified Maulem-van
			//Genuchten Formulation for Improved Description of the Hydraulic Conductivity Near Saturation’.
			//Written in MATLAB by Vishal Sharma (of Portland State) and modified for FORTRAN by Stephen Forner Summer 2010
			//This model is based on curve fit data that describes the capillary motion of the water in combination with the gravitational
			//forces on the water.
			//This set of equations is unstable if the time step is larger than given by a curve fit equation.  This first DO loop is to
			//see how many time steps are needed to be stable.
			//This method of moisture distribution relies on variables which are experimentally determined: alpha, lambda, n and the
			//hydraulic conductivity at saturation.

			//Now, solve for the soil parameters for  of the top soil layer

			RelativeSoilSaturationTop = ( Moisture - MoistureResidual ) / ( MoistureMax - MoistureResidual );
			if ( RelativeSoilSaturationTop < 0.0001 ) {
				if ( ErrIndex == 0 ) {
					ShowWarningMessage( "EcoRoof: UpdateSoilProps: Relative Soil Saturation Top Moisture <= 0.0001, Value=[" + RoundSigDigits( RelativeSoilSaturationTop, 5 ) + "]." );
					ShowContinueError( "Value is set to 0.0001 and simulation continues." );
					ShowContinueError( "You may wish to increase the number of timesteps to attempt to alleviate the problem." );
				}
				ShowRecurringWarningErrorAtEnd( "EcoRoof: UpdateSoilProps: Relative Soil Saturation Top Moisture < 0. continues", ErrIndex, RelativeSoilSaturationTop, RelativeSoilSaturationTop );
				RelativeSoilSaturationTop = 0.0001;
			}
			SoilHydroConductivityTop = SoilConductivitySaturation * std::pow( RelativeSoilSaturationTop, lambda ) * pow_2( 1.0 - std::pow( 1.0 - std::pow( RelativeSoilSaturationTop, n / ( n - 1.0 ) ), ( n - 1.0 ) / n ) );
			CapillaryPotentialTop = ( -1.0 / alpha ) * std::pow( std::pow( 1.0 / RelativeSoilSaturationTop, n / ( n - 1.0 ) ) - 1.0, 1.0 / n );

			//Then the soil parameters for the root soil layer
			RelativeSoilSaturationRoot = ( MeanRootMoisture - MoistureResidual ) / ( MoistureMax - MoistureResidual );
			SoilHydroConductivityRoot = SoilConductivitySaturation * std::pow( RelativeSoilSaturationRoot, lambda ) * pow_2( 1.0 - std::pow( 1.0 - std::pow( RelativeSoilSaturationRoot, n / ( n - 1.0 ) ), ( n - 1.0 ) / n ) );
			CapillaryPotentialRoot = ( -1.0 / alpha ) * std::pow( std::pow( 1.0 / RelativeSoilSaturationRoot, n / ( n - 1.0 ) ) - 1.0, 1.0 / n );

			//Next, using the soil parameters, solve for the soil moisture
			SoilConductivityAveTop = ( SoilHydroConductivityTop + SoilHydroConductivityRoot ) * 0.5;
			Moisture += ( SecondsPerTimeStep / TopDepth ) * ( ( SoilConductivityAveTop * ( CapillaryPotentialTop - CapillaryPotentialRoot ) / TopDepth ) - SoilConductivityAveTop );

			//Now limit the soil from going over the moisture maximum and takes excess to create runoff
			if ( Moisture >= MoistureMax ) { //This statement makes sure that the top layer is not over the moisture maximum for the soil.
				Moisture = 0.9999 * MoistureMax; //then it takes any moisture over the maximum amount and makes it runoff
				CurrentRunoff += ( Moisture - MoistureMax * 0.9999 ) * TopDepth;
			}

			//Now make sure that the soil does not go below the moisture minimum
			if ( Moisture <= ( 1.01 * MoistureResidual ) ) {
				Moisture = 1.01 * MoistureResidual;
			}

			//Next, solve the parameters for the bottom layer
			SoilConductivityAveRoot = SoilHydroConductivityRoot;

			//Now make sure the rate of liquid leaving the soil is more than one drop per hour
			if ( ( SoilConductivityAveRoot * 3600.0 ) <= ( 2.33e-7 ) ) {
				SoilConductivityAveRoot = 0.0;
			}

			//Using the parameters above, distribute the Root Layer moisture
			TestMoisture = MeanRootMoisture;
			MeanRootMoisture += ( SecondsPerTimeStep / RootDepth ) * ( ( SoilConductivityAveTop * ( CapillaryPotentialTop - CapillaryPotentialRoot ) / RootDepth ) + SoilConductivityAveTop - SoilConductivityAveRoot );

			//Limit the moisture from going over the saturation limit and create runoff:
			if ( MeanRootMoisture >= MoistureMax ) {
				MeanRootMoisture = 0.9999 * MoistureMax;
				CurrentRunoff += ( Moisture - MoistureMax * 0.9999 ) * RootDepth;
			}

			//Limit the soil from going below the soil saturation limit:
			if ( MeanRootMoisture <= ( 1.01 * MoistureResidual ) ) {
				MeanRootMoisture = 1.01 * MoistureResidual;
			}

			//Next, track runoff from the bottom of the soil:
			CurrentRunoff += SoilConductivityAveRoot * SecondsPerTimeStep;

			//~~~END SF EDITS
		}

		// NEXT Limit moisture values to saturation (create RUNOFF that we can track)
		// CurrentRunoff is sum of "overwatering" in a timestep and excess moisture content
		if ( ! WarmupFlag ) {
			CumRunoff += CurrentRunoff;
		}

		if ( MeanRootMoisture <= MoistureResidual * 1.00001 ) {
			Moisture -= ( MoistureResidual * 1.00001 - MeanRootMoisture ) * RootDepth / TopDepth;
			// If the plant has extracted more moisture than is in the root zone soil, then make it come from
			// the top layer rather than the root zone... unless top is also dry...
			if ( Moisture < MoistureResidual * 1.00001 ) Moisture = MoistureResidual * 1.00001;
			MeanRootMoisture = MoistureResidual * 1.00001; // Need to make sure we do not divide by zero later.
		}

		// ------------------------------------------------------------------------------------------
		// Having updated moisture values now we modify soil thermal properties based on moisture content

		// NOTE: Variables SoilAbsorpSolar, SoilDensity, SoilSpecHeat, and SoilConductivity are the values
		// that the soil properties OUGHT to attain for the current moisture level. These values are then
		// moderated using the TestRatio variable so that from one time step to the next no variable
		// changes by more than a certain percentage (typically 5-20%).

		// Note wet soil absorptance is generally 25-50% higher than dry soil absorptance (assume linear)
		SoilAbsorpSolar = DryAbsorp + ( 0.92 - DryAbsorp ) * ( Moisture - MoistureResidual ) / ( MoistureMax - MoistureResidual );
		// Limit solar absorptivity to 95% so soil abledo is always above 5%
		if ( SoilAbsorpSolar > 0.95 ) SoilAbsorpSolar = 0.95;
		// Limit solar absorptivity to greater than 20% so that albedo is always less than 80%
		if ( SoilAbsorpSolar < 0.20 ) SoilAbsorpSolar = 0.20;

		// Need to use for albedo in CalcEcoroof
		TestRatio = ( 1.0 - SoilAbsorpSolar ) / Alphag;
		if ( TestRatio > RatioMax ) TestRatio = RatioMax;
		if ( TestRatio < RatioMin ) TestRatio = RatioMin;
		Alphag *= TestRatio; //  included 1.0 - to make it albedo rather than SW absorptivity
		// Note wet soil density is calculated by simply adding the mass of water...
		AvgMoisture = ( RootDepth * MeanRootMoisture + TopDepth * Moisture ) / SoilThickness;
		SoilDensity = DryDens + ( AvgMoisture - MoistureResidual ) * 990.0;
		// Note 990 kg/m^3 is water density and the moisture is depth-averaged

		// Note wet soil has specific heat that is 40% higher than dry soil (assume linear)
		// OLD ::  SoilSpecHeat = DrySpecHeat*(1.0d0+ 0.4d0*(AvgMoisture-MoistureResidual)/(MoistureMax-MoistureResidual))
		// This is now based on Melos Hagos's results for C (March 2009)
		//    SoilSpecHeat = DrySpecHeat + 3.09*(AvgMoisture) CLEARLY in ERROR BY FACTOR of 1000
		//    DJS - Melos report has Spec = Cdry + 1.9 theta (where C is in kJ/kg/K), so...
		SoilSpecHeat = DrySpecHeat + 1900.0 * AvgMoisture;

		// Note wet soil has thermal conductivity that is up to 3 times that of  dry soil ...
		// For now simply let it DOUBLE over the range of moisture

		// Note wet soil has thermal conductivity that is up to 3 times that of  dry soil ...
		// OLD :: SoilConductivity = DryCond* (1.0d0 + 1.0d0 * (AvgMoisture-MoistureResidual)/(MoistureMax-MoistureResidual))
		// This is now based on Melos Hagos's results for k/kdry (March 2009)
		SatRatio = ( AvgMoisture - MoistureResidual ) / ( MoistureMax - MoistureResidual );
		SoilConductivity = ( DryCond / 1.15 ) * ( 1.45 * std::exp( 4.411 * SatRatio ) ) / ( 1.0 + 0.45 * std::exp( 4.411 * SatRatio ) );
		// DJS 2009 - note, this allows the actual conductivity to dip a little below the dry value... corresponding to
		// DJS 2009 - "bone dry" if you will, when moisture --> residual value.

		// HERE WE RE-RUN THE CONDUCTION TRANSFER FUNCTION (CTF) CALCULATIONS

		// NOTE: CTF uses the original Material( )%xxx variables, so must update these for current construction and
		//       time step...
		// TestRatio variable is available just in case there are stability issues. If so, we can limit the amount
		// by which soil properties are allowed to vary in one time step (10% in example below).

		TestRatio = SoilConductivity / Material( Construct( ConstrNum ).LayerPoint( 1 ) ).Conductivity;
		if ( TestRatio > RatioMax ) TestRatio = RatioMax;
		if ( TestRatio < RatioMin ) TestRatio = RatioMin;
		Material( Construct( ConstrNum ).LayerPoint( 1 ) ).Conductivity *= TestRatio;
		SoilConductivity = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).Conductivity;

		TestRatio = SoilDensity / Material( Construct( ConstrNum ).LayerPoint( 1 ) ).Density;
		if ( TestRatio > RatioMax ) TestRatio = RatioMax;
		if ( TestRatio < RatioMin ) TestRatio = RatioMin;
		Material( Construct( ConstrNum ).LayerPoint( 1 ) ).Density *= TestRatio;
		SoilDensity = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).Density;

		TestRatio = SoilSpecHeat / Material( Construct( ConstrNum ).LayerPoint( 1 ) ).SpecHeat;
		if ( TestRatio > RatioMax ) TestRatio = RatioMax;
		if ( TestRatio < RatioMin ) TestRatio = RatioMin;
		Material( Construct( ConstrNum ).LayerPoint( 1 ) ).SpecHeat *= TestRatio;
		SoilSpecHeat = Material( Construct( ConstrNum ).LayerPoint( 1 ) ).SpecHeat;

		// Now call InitConductionTransferFunction with the ConstrNum as the argument. As long as the argument is
		// non-zero InitConductionTransferFunction will ONLY update this construction. If the argument is 0 it will
		// rerun the ENTIRE InitConductionTransferFunction on all constructions (as in initial code start-up mode).

		// DJS The following works for most simulations, but has stability issues in some cases.
		// DJS - in the present version it seems best to NOT update soil thermal properties.
		//   Call InitConductionTransferFunctions(ConstrNum)
		// DJS In future revision we will implement these modified soil thermal properties in the finite difference
		// solution scheme.

		// DJS - Note the following write/format statements should be commented out prior to releasing within EnergyPlus
		// DJS - they are handy in doing hourly validation/evaluations, though, so leave them in for future development.

		//   write(unit,799) DayofYear, HourOfDay, Qsoil,Tg, Tf, Moisture, MeanRootMoisture,CumPrecip &
		//  ,CumET,CumRunoff, CumIrrigation, SoilDensity, SoilSpecHeat,SoilConductivity,Alphag
		//799 format(' ',I3,' ',I3,' ',' ',f9.3,' ',f6.2,' ',f6.2,' ',f5.3,' ',f5.3,' ',f6.4, '  '  &
		//    f7.3, ' ', f7.3, ' ',f7.3, ' ',f6.1,' ',f7.1,'  ',f6.3,'  ',f6.2)

	}

	// *****************************************************************************

	//     NOTICE

	//     Copyright © 1996-2014 The Board of Trustees of the University of Illinois
	//     and The Regents of the University of California through Ernest Orlando Lawrence
	//     Berkeley National Laboratory.  All rights reserved.

	//     Portions of the EnergyPlus software package have been developed and copyrighted
	//     by other individuals, companies and institutions.  These portions have been
	//     incorporated into the EnergyPlus software package under license.   For a complete
	//     list of contributors, see "Notice" located in main.cc.

	//     NOTICE: The U.S. Government is granted for itself and others acting on its
	//     behalf a paid-up, nonexclusive, irrevocable, worldwide license in this data to
	//     reproduce, prepare derivative works, and perform publicly and display publicly.
	//     Beginning five (5) years after permission to assert copyright is granted,
	//     subject to two possible five year renewals, the U.S. Government is granted for
	//     itself and others acting on its behalf a paid-up, non-exclusive, irrevocable
	//     worldwide license in this data to reproduce, prepare derivative works,
	//     distribute copies to the public, perform publicly and display publicly, and to
	//     permit others to do so.

	//     TRADEMARKS: EnergyPlus is a trademark of the US Department of Energy.

} // EcoRoofManager

} // EnergyPlus
