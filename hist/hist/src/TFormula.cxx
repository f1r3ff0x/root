// @(#)root/hist:$Id$
// Author: Maciej Zimnoch 30/09/2013

/*************************************************************************
 * Copyright (C) 1995-2013, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#if __cplusplus >= 201103L
#define ROOT_CPLUSPLUS11 1
#endif

#include "TROOT.h"
#include "TClass.h"
#include "TMethod.h"
#include "TMath.h"
#include "TMethodCall.h"
#include <TBenchmark.h>
#include "TError.h"
#include "TInterpreter.h"
#include "TFormula.h"
#include <cassert>
#include <iostream>

// #define __STDC_LIMIT_MACROS
// #define __STDC_CONSTANT_MACROS

// #include  "cling/Interpreter/Interpreter.h"
// #include  "cling/Interpreter/Value.h"
// #include  "cling/Interpreter/StoredValueRef.h"


#ifdef WIN32
#pragma optimize("",off)
#endif

ClassImp(TFormula)
//______________________________________________________________________________
//*-*-*-*-*-*-*-*-*-*-*The  F O R M U L A  class*-*-*-*-*-*-*-*-*-*-*-*-*-*-*
//*-*                  =========================
//*-*
//*-*   This class has been implemented during Google Summer of Code 2013 by Maciej Zimnoch.
//*-*   =========================================================
//Begin_Html
/*
<img src="gif/tformula_classtree.gif">
*/
//End_Html
//*-*
//*-*  Example of valid expressions:
//*-*     -  sin(x)/x
//*-*     -  [0]*sin(x) + [1]*exp(-[2]*x)
//*-*     -  x + y**2
//*-*     -  x^2 + y^2
//*-*     -  [0]*pow([1],4)
//*-*     -  2*pi*sqrt(x/y)
//*-*     -  gaus(0)*expo(3)  + ypol3(5)*x
//*-*     -  gausn(0)*expo(3) + ypol3(5)*x
//*-*
//*-*  In the last example above:
//*-*     gaus(0) is a substitute for [0]*exp(-0.5*((x-[1])/[2])**2)
//*-*        and (0) means start numbering parameters at 0
//*-*     gausn(0) is a substitute for [0]*exp(-0.5*((x-[1])/[2])**2)/(sqrt(2*pi)*[2]))
//*-*        and (0) means start numbering parameters at 0
//*-*     expo(3) is a substitute for exp([3]+[4]*x)
//*-*     pol3(5) is a substitute for par[5]+par[6]*x+par[7]*x**2+par[8]*x**3
//*-*         (PolN stands for Polynomial of degree N)
//*-*
//*-*   TMath functions can be part of the expression, eg:
//*-*     -  TMath::Landau(x)*sin(x)
//*-*     -  TMath::Erf(x)
//*-*
//*-*   Formula may contain constans, eg:
//*-*    - sqrt2 
//*-*    - e  
//*-*    - pi 
//*-*    - ln10 
//*-*    - infinity
//*-*      and more.
//*-*   
//*-*   Comparisons operators are also supported (&&, ||, ==, <=, >=, !)
//*-*   Examples:
//*-*      sin(x*(x<0.5 || x>1))
//*-*   If the result of a comparison is TRUE, the result is 1, otherwise 0.
//*-*
//*-*   Already predefined names can be given. For example, if the formula
//*-*     TFormula old("old",sin(x*(x<0.5 || x>1))) one can assign a name to the formula. By default
//*-*     the name of the object = title = formula itself.
//*-*     TFormula new("new","x*old") is equivalent to:
//*-*     TFormula new("new","x*sin(x*(x<0.5 || x>1))")
//*-*
//*-*   Class supports unlimited numer of variables and parameters.
//*-*   By default it has 4 variables(indicated by x,y,z,t) and no parameters.
//*-*
//*-*   This class is the base class for the function classes TF1,TF2 and TF3.
//*-*   It is also used by the ntuple selection mechanism TNtupleFormula.
//*-*
//*-*
//*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*

// prefix used for function name passed to Cling    
static const TString gNamePrefix = "T__"; 

Bool_t TFormula::IsOperator(const char c)
{
   char ops[] = { '+','^', '-','/','*','<','>','|','&','!','='};
   Int_t opsLen = sizeof(ops)/sizeof(char);
   for(Int_t i = 0; i < opsLen; ++i)
      if(ops[i] == c)
         return true;
   return false;
}

Bool_t TFormula::IsBracket(const char c)
{
   char brackets[] = { ')','(','{','}'};
   Int_t bracketsLen = sizeof(brackets)/sizeof(char);
   for(Int_t i = 0; i < bracketsLen; ++i)
      if(brackets[i] == c)
         return true;
   return false;
}

Bool_t TFormula::IsFunctionNameChar(const char c)
{
   return !IsBracket(c) && !IsOperator(c) && c != ',';
}

Bool_t TFormula::IsDefaultVariableName(const TString &name)
{
   return name == "x" || name == "z" || name == "y" || name == "t";
}

TFormula::TFormula()
{
   fName = "";
   fTitle = "";
   fClingInput = "";
   fReadyToExecute = false;
   fClingInitialized = false;
   fAllParametersSetted = false;
   fMethod = 0;
   fNdim = 0;
   fNpar = 0;
   fNumber = 0;
   fClingName = "";
   fFormula = "";
}

TFormula::~TFormula()
{
   if(fMethod)
   {  
      fMethod->Delete();
   }
   int nLinParts = fLinearParts.GetSize(); 
   if (nLinParts > 0) { 
      for (int i = 0; i < nLinParts; ++i) delete fLinearParts[i]; 
      fLinearParts.Clear();
   }
}

TFormula::TFormula(const char *name, Int_t nparams, Int_t ndims)
{
   //*-*     
   //*-*  Constructor
   //*-*  When TF1 is constructed using C++ function, TF1 need space to keep parameters values.
   //*-*  

   fName = name;
   fTitle = "";
   fClingInput = "";
   fReadyToExecute = false;
   fClingInitialized = false;
   fAllParametersSetted = false;
   fMethod = 0;
   fNdim = ndims;
   fNpar = 0;
   fNumber = 0;
   fClingName = "";
   fFormula = "";
   FillDefaults();
   for(Int_t i = 0; i < nparams; ++i)
   {
      TString parName = TString::Format("%d",i);
      DoAddParameter(parName,0,false);
   }
}

TFormula::TFormula(const TString &name, TString formula)
   :fClingInput(formula),fFormula(formula)
{
   fReadyToExecute = false;
   fClingInitialized = false;
   fMethod = 0;
   TNamed(fName,formula);
   fNdim = 0;
   fNpar = 0;
   fNumber = 0;
   FillDefaults();

   fName = gNamePrefix + name;

   TFormula *old = (TFormula*)gROOT->GetListOfFunctions()->FindObject(fName);
   if (old) 
   {
      gROOT->GetListOfFunctions()->Remove(old);
   }
   if (name == "x" || name == "y" || name == "z" || name == "t")
   {
      Error("TFormula","The name %s is reserved as a TFormula variable name.\n",name.Data());
   } else 
   {

      gROOT->GetListOfFunctions()->Add(this);

   }
   PreProcessFormula(fFormula);
   //std::cout << "formula " << GetName() << " is preprocessed " << std::endl;

   fClingInput = fFormula;
   PrepareFormula(fClingInput);


   //std::cout << "formula " << GetName() << " is prepared " << std::endl;

}

TFormula::TFormula(const TFormula &formula) : TNamed(formula.GetName(),formula.GetTitle())
{
   fReadyToExecute = false;
   fClingInitialized = false;
   fMethod = 0;
   fNdim = formula.GetNdim();
   fNpar = formula.GetNpar();
   fNumber = formula.GetNumber();
   fFormula = formula.GetExpFormula();

   FillDefaults();
   fName = gNamePrefix + formula.GetName();

   TFormula *old = (TFormula*)gROOT->GetListOfFunctions()->FindObject(formula.GetName());
   if (old) 
   {
      gROOT->GetListOfFunctions()->Remove(old);
   }
   if (strcmp(formula.GetName(),"x") == 0 || strcmp(formula.GetName(),"y") == 0 ||
       strcmp(formula.GetName(),"z") == 0 || strcmp(formula.GetName(),"t") == 0)
   {
      Error("TFormula","The name %s is reserved as a TFormula variable name.\n",formula.GetName());
   } else 
   {
      gROOT->GetListOfFunctions()->Add(this);
   }
   PreProcessFormula(fFormula);
   fClingInput = fFormula;
   PrepareFormula(fClingInput);
}

TFormula& TFormula::operator=(const TFormula &rhs)
{
   //*-*    
   //*-*  = Operator    
   //*-*    

   if (this != &rhs) {
      rhs.Copy(*this);
   }
   return *this;
}
void TFormula::Copy(TObject &obj) const
{
   // need to copy also cling parameters

   ((TFormula&)obj).fClingParameters = fClingParameters;
   ((TFormula&)obj).fClingVariables = fClingVariables;

   ((TFormula&)obj).fFuncs = fFuncs;
   ((TFormula&)obj).fVars = fVars;
   ((TFormula&)obj).fParams = fParams;
   ((TFormula&)obj).fConsts = fConsts;
   ((TFormula&)obj).fFunctionsShortcuts = fFunctionsShortcuts;
   ((TFormula&)obj).fFormula  = fFormula;
   ((TFormula&)obj).fNdim = fNdim;
   ((TFormula&)obj).fNpar = fNpar;
   ((TFormula&)obj).fNumber = fNumber;
   ((TFormula&)obj).fLinearParts = fLinearParts;
   ((TFormula&)obj).SetParameters(GetParameters());

   ((TFormula&)obj).fClingInput = fClingInput;
   ((TFormula&)obj).fReadyToExecute = fReadyToExecute;
   ((TFormula&)obj).fClingInitialized = fClingInitialized;
   ((TFormula&)obj).fAllParametersSetted = fAllParametersSetted;
   ((TFormula&)obj).fClingName = fClingName;

   if (fMethod) {
      if (((TFormula&)obj).fMethod) delete ((TFormula&)obj).fMethod;
      // use copy-constructor of TMethodCall
      TMethodCall *m = new TMethodCall(*fMethod);
      ((TFormula&)obj).fMethod  = m;
   }

   ((TFormula&)obj).fFuncPtr = fFuncPtr;

}
void TFormula::PrepareEvalMethod()
{
   //*-*    
   //*-*    Sets TMethodCall to function inside Cling environment
   //*-*    TFormula uses it to execute function.
   //*-*    After call, TFormula should be ready to evaluate formula.   
   //*-*    
   if(!fMethod)
   {
      fMethod = new TMethodCall();

      Bool_t hasParameters = (fNpar > 0);
      Bool_t hasVariables  = (fNdim > 0);
      TString prototypeArguments = "";
      if(hasVariables)
      {
         prototypeArguments.Append("Double_t*");
      }
      if(hasVariables && hasParameters)
      {
         prototypeArguments.Append(",");
      }
      if(hasParameters)
      {
         prototypeArguments.Append("Double_t*");
      }
      // init method call using real function name (cling name) which is defined in ProcessFormula
      fMethod->InitWithPrototype(fClingName,prototypeArguments);
      if(!fMethod->IsValid())
      {
         Error("Eval","Can't find %s function prototype with arguments %s",fClingName.Data(),prototypeArguments.Data());
         return ;
      }

      // not needed anymore since we use the function pointer 
      // if(hasParameters)
      // {
      //    Long_t args[2];
      //    args[0] = (Long_t)fClingVariables.data();
      //    args[1] = (Long_t)fClingParameters.data();
      //    fMethod->SetParamPtrs(args,2);
      // }
      // else
      // {
      //    Long_t args[1];
      //    args[0] = (Long_t)fClingVariables.data();
      //    fMethod->SetParamPtrs(args,1);
      // }

      CallFunc_t * callfunc = fMethod->GetCallFunc();
      TInterpreter::CallFuncIFacePtr_t faceptr = gCling->CallFunc_IFacePtr(callfunc);
      fFuncPtr = faceptr.fGeneric; 
   }
}

void TFormula::InputFormulaIntoCling()
{
   //*-*    
   //*-*    Inputs formula, transfered to C++ code into Cling
   //*-*    
   if(!fClingInitialized && fReadyToExecute && fClingInput.Length() > 0)
   {
      char rawInputOn[]  = ".rawInput 1";
      char rawInputOff[] = ".rawInput 0";
      gCling->ProcessLine(rawInputOn);
      gCling->ProcessLine(fClingInput);
      gCling->ProcessLine(rawInputOff);
      PrepareEvalMethod();
      fClingInitialized = true;

      // // store function pointer 
      // TString funcAddress = "&" + TString(GetName() );
      // cling::StoredValueRef valref;
      // cling::runtime::gCling->evaluate(funcAddress,valref);
      // typedef Double_t (* FuncPointerType)(Double_t *, Double_t *);
      // void * ptr = valref.get().getAs(&ptr);
      // FuncPointerType fFuncPointer = (FuncPointerType) ptr; 
   }
}
void TFormula::FillDefaults()
{
   //*-* 
   //*-*    Fill structures with default variables, constants and function shortcuts
   //*-*    
//#ifdef ROOT_CPLUSPLUS11
   
   const TString defvars[] = { "x","y","z","t"};
   const pair<TString,Double_t> defconsts[] = { {"pi",TMath::Pi()}, {"sqrt2",TMath::Sqrt2()},
         {"infinity",TMath::Infinity()}, {"e",TMath::E()}, {"ln10",TMath::Ln10()},
         {"loge",TMath::LogE()}, {"c",TMath::C()}, {"g",TMath::G()}, 
         {"h",TMath::H()}, {"k",TMath::K()},{"sigma",TMath::Sigma()},
         {"r",TMath::R()}, {"eg",TMath::EulerGamma()},{"true",1},{"false",0} }; 
   const pair<TString,TString> funShortcuts[] = { {"sin","TMath::Sin" },
         {"cos","TMath::Cos" }, {"exp","TMath::Exp"}, {"log","TMath::Log"},
         {"tan","TMath::Tan"}, {"sinh","TMath::SinH"}, {"cosh","TMath::CosH"},
         {"tanh","TMath::TanH"}, {"asin","TMath::ASin"}, {"acos","TMath::ACos"},
         {"atan","TMath::ATan"}, {"atan2","TMath::ATan2"}, {"sqrt","TMath::Sqrt"},
         {"ceil","TMath::Ceil"}, {"floor","TMath::Floor"}, {"pow","TMath::Power"},
         {"binomial","TMath::Binomial"},{"abs","TMath::Abs"} }; 

   std::vector<TString> defvars2(10);
   for (int i = 0; i < 9; ++i) 
      defvars2[i] = TString::Format("x[%d]",i);

   for(auto var : defvars)
   {
      int pos = fVars.size();
      fVars[var] = TFormulaVariable(var,0,pos);
      fClingVariables.push_back(0);
   }
   // add also the variables definesd like x[0],x[1],x[2],...
   // support up to x[9] - if needed extend that to higher value 
   // const int maxdim = 10;
   // for (int i = 0; i < maxdim;  ++i) {
   //    TString xvar = TString::Format("x[%d]",i);
   //    fVars[xvar] =  TFormulaVariable(xvar,0,i);
   //    fClingVariables.push_back(0);
   // }

   for(auto con : defconsts)
   {
      fConsts[con.first] = con.second;
   }
   for(auto fun : funShortcuts)
   {
      fFunctionsShortcuts[fun.first] = fun.second;
   }

/*** - old code tu support C++03 
#else

   TString  defvarsNames[] = {"x","y","z","t"};
   Int_t    defvarsLength = sizeof(defvarsNames)/sizeof(TString);

   TString  defconstsNames[] = {"pi","sqrt2","infinity","e","ln10","loge","c","g","h","k","sigma","r","eg","true","false"};
   Double_t defconstsValues[] = {TMath::Pi(),TMath::Sqrt2(),TMath::Infinity(),TMath::E(),TMath::Ln10(),TMath::LogE(),
                                 TMath::C(),TMath::G(),TMath::H(),TMath::K(),TMath::Sigma(),TMath::R(),TMath::EulerGamma(), 1, 0};
   Int_t    defconstsLength = sizeof(defconstsNames)/sizeof(TString);

   TString  funShortcutsNames[] = {"sin","cos","exp","log","tan","sinh","cosh","tanh","asin","acos","atan","atan2","sqrt",
                                  "ceil","floor","pow","binomial","abs"};
   TString  funShortcutsExtendedNames[] = {"TMath::Sin","TMath::Cos","TMath::Exp","TMath::Log","TMath::Tan","TMath::SinH",
                                           "TMath::CosH","TMath::TanH","TMath::ASin","TMath::ACos","TMath::ATan","TMath::ATan2",
                                           "TMath::Sqrt","TMath::Ceil","TMath::Floor","TMath::Power","TMath::Binomial","TMath::Abs"};
   Int_t    funShortcutsLength = sizeof(funShortcutsNames)/sizeof(TString);                                           

   for(Int_t i = 0; i < defvarsLength; ++i)
   {
      TString var = defvarsNames[i];
      Double_t value = 0;
      unsigned int pos = fVars.size();
      fVars[var] = TFormulaVariable(var,value,pos);
      fClingVariables.push_back(value);
   }

   for(Int_t i = 0; i < defconstsLength; ++i)
   {
      fConsts[defconstsNames[i]] = defconstsValues[i];
   }
   for(Int_t i = 0; i < funShortcutsLength; ++i)
   {
      pair<TString,TString> fun(funShortcutsNames[i],funShortcutsExtendedNames[i]);
      fFunctionsShortcuts[fun.first] = fun.second;
   }

#endif 
***/

}

void TFormula::HandlePolN(TString &formula)
{
   //*-*    
   //*-*    Handling polN
   //*-*    If before 'pol' exist any name, this name will be treated as variable used in polynomial
   //*-*    eg.
   //*-*    varpol2(5) will be replaced with: [5] + [6]*var + [7]*var^2
   //*-*    Empty name is treated like variable x.
   //*-*    
   Int_t polPos = formula.Index("pol");
   while(polPos != kNPOS)
   {
      SetBit(kLinear,1);

      Bool_t defaultVariable = false;
      TString variable;
      Int_t openingBracketPos = formula.Index('(',polPos);
      Bool_t defaultCounter = (openingBracketPos == kNPOS);
      Bool_t defaultDegree = true;
      Int_t degree,counter;
      if(!defaultCounter)
      {
          degree = TString(formula(polPos + 3,openingBracketPos - polPos - 3)).Atoi();
          counter = TString(formula(openingBracketPos+1,formula.Index(')',polPos) - openingBracketPos)).Atoi(); 
      }
      else
      {
         Int_t temp = polPos+3;
         while(temp < formula.Length() && isdigit(formula[temp]))
         {
            defaultDegree = false;
            temp++;
         }
         degree = TString(formula(polPos+3,temp - polPos - 3)).Atoi();
         counter = 0;
      }
      fNumber = 300 + degree;
      TString replacement = TString::Format("[%d]",counter);
      if(polPos - 1 < 0 || !IsFunctionNameChar(formula[polPos-1]))
      {
         variable = "x";
         defaultVariable = true;
      }
      else
      {
         Int_t tmp = polPos - 1;
         while(tmp >= 0 && IsFunctionNameChar(formula[tmp]))
         {
            tmp--;
         }
         variable = formula(tmp + 1, polPos - (tmp+1));
      }
      Int_t param = counter + 1;
      Int_t tmp = 1;
      while(tmp <= degree)
      {
         replacement.Append(TString::Format("+[%d]*%s^%d",param,variable.Data(),tmp));
         param++;
         tmp++;
      }
      TString pattern;
      if(defaultCounter && !defaultDegree)
      {
         pattern = TString::Format("%spol%d",(defaultVariable ? "" : variable.Data()),degree);
      }
      else if(defaultCounter && defaultDegree)
      {
         pattern = TString::Format("%spol",(defaultVariable ? "" : variable.Data()));
      }
      else
      {
         pattern = TString::Format("%spol%d(%d)",(defaultVariable ? "" : variable.Data()),degree,counter);   
      }
      
      formula.ReplaceAll(pattern,replacement);
      polPos = formula.Index("pol");
   }
}
void TFormula::HandleParametrizedFunctions(TString &formula)
{
   //*-*    
   //*-*    Handling parametrized functions
   //*-*    Function can be normalized, and have different variable then x.
   //*-*    Variables should be placed in brackets after function name.
   //*-*    No brackets are treated like [x].
   //*-*    Normalized function has char 'n' after name, eg.
   //*-*    gausn[var](0) will be replaced with [0]*exp(-0.5*((var-[1])/[2])^2)/(sqrt(2*pi)*[2])
   //*-*    
   //*-*    Adding function is easy, just follow these rules:
   //*-*    - Key for function map is pair of name and dimension of function
   //*-*    - value of key is a pair function body and normalized function body
   //*-*    - {Vn} is a place where variable appear, n represents n-th variable from variable list.
   //*-*      Count starts from 0.
   //*-*    - [num] stands for parameter number. 
   //*-*      If user pass to function argument 5, num will stand for (5 + num) parameter.
   //*-*    

   map< pair<TString,Int_t> ,pair<TString,TString> > functions; 
   functions.insert(make_pair(make_pair("gaus",1),make_pair("[0]*exp(-0.5*(({V0}-[1])/[2])*(({V0}-[1])/[2]))","[0]*exp((({V0}-[1])/[2])*(({V0}-[1])/[2]))/(sqrt(2*pi)*[2])")));
   functions.insert(make_pair(make_pair("landau",1),make_pair("[0]*TMath::Landau({V0},[1],[2],false)","[0]*TMath::Landau({V0},[1],[2],true)")));
   functions.insert(make_pair(make_pair("expo",1),make_pair("exp([0]+[1]*{V0})","")));
   // 2-dimensional functions
   functions.insert(make_pair(make_pair("gaus",2),make_pair("[0]*exp(-0.5*(({V0}-[1])/[2])^2 - 0.5*(({V1}-[3])/[4])^2)","")));
   functions.insert(make_pair(make_pair("landau",2),make_pair("[0]*TMath::Landau({V0},[1],[2],false)*TMath::Landau({V1},[3],[4],false)","")));
   functions.insert(make_pair(make_pair("expo",2),make_pair("exp([0]+[1]*{V0})","exp([0]+[1]*{V0}+[2]*{V1})")));

   map<TString,Int_t> functionsNumbers;
   functionsNumbers["gaus"] = 100;
   functionsNumbers["landau"] = 200;
   functionsNumbers["expo"] = 400;

   // replace old names xygaus -> gaus[x,y]
   formula.ReplaceAll("xygaus","gaus[x,y]");
   formula.ReplaceAll("xylandau","landau[x,y]");
   formula.ReplaceAll("xyexpo","expo[x,y]");

   for(map<pair<TString,Int_t>,pair<TString,TString> >::iterator it = functions.begin(); it != functions.end(); ++it) 
   {

      TString funName = it->first.first;
      Int_t funPos = formula.Index(funName);

      //std::cout << formula << " ---- " << funName << "  " << funPos << std::endl;
      while(funPos != kNPOS)
      {
         fNumber = functionsNumbers[funName];
         // check if function is normalized by looking at "n" character after function name (e.g. gausn)
         Bool_t isNormalized = (formula[funPos + funName.Length()] == 'n');
         if(isNormalized)
         {
            SetBit(kNormalized,1);
         }
         TString *variables = 0;
         Int_t dim = 0;
         TString varList = "";
         Bool_t defaultVariable = false;

         // check if function has specified the [...] e.g. gaus[x,y]
         Int_t openingBracketPos = funPos + funName.Length() + (isNormalized ? 1 : 0);
         Int_t closingBracketPos = kNPOS;
         if(openingBracketPos > formula.Length() || formula[openingBracketPos] != '[')
         {
            dim = 1;
            variables = new TString[dim];
            variables[0] = "x";
            defaultVariable = true;
         }
         else 
         {
            // in case of [..] found, assume they specify all the variables. Use it to get function dimension
            closingBracketPos = formula.Index(']',openingBracketPos);
            varList = formula(openingBracketPos+1,closingBracketPos - openingBracketPos - 1);
            dim = varList.CountChar(',') + 1;
            variables = new TString[dim];
            Int_t Nvar = 0;
            TString varName = "";
            for(Int_t i = 0 ; i < varList.Length(); ++i)
            {
               if(IsFunctionNameChar(varList[i]))
               {
                  varName.Append(varList[i]);
               }
               if(varList[i] == ',')
               {
                  variables[Nvar] = varName;
                  varName = "";
                  Nvar++;
               }  
            }
            if(varName != "") // we will miss last variable
            {
               variables[Nvar] = varName;
            }
         }
         // chech if dimension obtained from [...] is compatible with existing pre-defined functions 
         if(dim != it->first.second)
         {
            pair<TString,Int_t> key = make_pair(funName,dim);
            if(functions.find(key) == functions.end())
            {
               Error("PreProcessFormula","%d dimension function %s is not defined as parametrized function.",dim,funName.Data());
               return;
            }
            break;
         }
         // look now for the (..) brackets to get the parameter counter (e.g. gaus(0) + gaus(3) )
         // need to start for a position 
         Int_t openingParenthesisPos = (closingBracketPos == kNPOS) ? openingBracketPos : closingBracketPos + 1;
         bool defaultCounter = (openingParenthesisPos > formula.Length() || formula[openingParenthesisPos] != '(');

         //Int_t openingParenthesisPos = formula.Index('(',funPos);
         //Bool_t defaultCounter = (openingParenthesisPos == kNPOS);
         Int_t counter;
         if(defaultCounter)
         {
            counter = 0;           
         }
         else
         {
            counter = TString(formula(openingParenthesisPos+1,formula.Index(')',funPos) - openingParenthesisPos -1)).Atoi(); 
         }
         //std::cout << "openingParenthesisPos  " << openingParenthesisPos << " counter is " << counter <<  std::endl; 

         TString body = (isNormalized ? it->second.second : it->second.first);
         if(isNormalized && body == "")
         {
            Error("PreprocessFormula","%d dimension function %s has no normalized form.",it->first.second,funName.Data());
            break;
         }
         for(int i = 0 ; i < body.Length() ; ++i)
         {
            if(body[i] == '{')
            {
               // replace {Vn} with variable names
               i += 2; // skip '{' and 'V'
               Int_t num = TString(body(i,body.Index('}',i) - i)).Atoi();
               TString variable = variables[num];
               TString pattern = TString::Format("{V%d}",num);
               i -= 2; // restore original position
               body.Replace(i, pattern.Length(),variable,variable.Length());
               i += variable.Length()-1;   // update i to reflect change in body string
            }
            else if(body[i] == '[')
            {
               // update parameter counters in case of many functions (e.g. gaus(0)+gaus(3) ) 
               Int_t tmp = i;
               while(tmp < body.Length() && body[tmp] != ']')
               {
                  tmp++;
               }
               Int_t num = TString(body(i+1,tmp - 1 - i)).Atoi();
               num += counter;
               TString replacement = TString::Format("%d",num);
              
               body.Replace(i+1,tmp - 1 - i,replacement,replacement.Length());
               i += replacement.Length() + 1;
            }
         }
         TString pattern;
         if(defaultCounter && defaultVariable)
         {
            pattern = TString::Format("%s%s",
                           funName.Data(),
                           (isNormalized ? "n" : ""));
         }
         if(!defaultCounter && defaultVariable)
         {
            pattern = TString::Format("%s%s(%d)",
                           funName.Data(),
                           (isNormalized ? "n" : ""),
                           counter);
         }
         if(defaultCounter && !defaultVariable)
         {
            pattern = TString::Format("%s%s[%s]",
                           funName.Data(),
                           (isNormalized ? "n":""),
                           varList.Data());
         }
         if(!defaultCounter && !defaultVariable)
         {
            pattern = TString::Format("%s%s[%s](%d)",
                           funName.Data(),
                           (isNormalized ? "n" : ""),
                           varList.Data(),
                           counter);
         }
         TString replacement = body;
         
         formula.Replace(funPos,pattern.Length(),replacement,replacement.Length());

         funPos = formula.Index(funName);
      }
      //std::cout << " formula is now " << formula << std::endl;
   }
}
void TFormula::HandleExponentiation(TString &formula)
{
   //*-*    
   //*-*    Handling exponentiation
   //*-*    Can handle multiple carets, eg.
   //*-*    2^3^4 will be treated like 2^(3^4)    
   //*-*    
   Int_t caretPos = formula.Last('^');
   while(caretPos != kNPOS)
   {

      TString right,left;
      Int_t temp = caretPos;
      temp--;
      if(formula[temp] == ')')
      {
         Int_t depth = 1;
         temp--;
         while(depth != 0 && temp > 0)
         {
            if(formula[temp] == ')')
               depth++;
            if(formula[temp] == '(')
               depth--;
            temp--;
         }
         temp++;
      }
      while(temp >= 0 && !IsOperator(formula[temp]))
      {
         temp--;
      }
      left = formula(temp + 1, caretPos - (temp + 1));

      temp = caretPos;
      temp++;
      if(formula[temp] == '(')
      {
         Int_t depth = 1;
         temp++;
         while(depth != 0 && temp < formula.Length())
         {
            if(formula[temp] == ')')
               depth--;
            if(formula[temp] == '(')
               depth++;
            temp++;
         }
         temp--;
      }
      while(temp < formula.Length() && !IsOperator(formula[temp]))
      {
         temp++;
      }
      right = formula(caretPos + 1, (temp - 1) - caretPos );

      TString pattern = TString::Format("%s^%s",left.Data(),right.Data());
      TString replacement = TString::Format("pow(%s,%s)",left.Data(),right.Data());
      formula.ReplaceAll(pattern,replacement);

      caretPos = formula.Last('^');
   }
}

void TFormula::HandleLinear(TString &formula)
{
   formula.ReplaceAll("++","@");
   Int_t linPos = formula.Index("@");
   Int_t NofLinParts = formula.CountChar((int)'@');
   fLinearParts.Expand(NofLinParts * 2);
   Int_t Nlinear = 0;
   while(linPos != kNPOS)
   {
      SetBit(kLinear,1);
      Int_t temp = linPos - 1;
      while(temp >= 0 && formula[temp] != '@')
      {
         temp--;
      }
      TString left = formula(temp+1,linPos - (temp +1));
      temp = linPos + 1;
      while(temp < formula.Length() && formula[temp] != '@')
      {
         temp++;
      }
      TString right = formula(linPos+1,temp - (linPos+1));
      TString pattern = TString::Format("%s@%s",left.Data(),right.Data());
      TString replacement = TString::Format("([%d]*(%s))+([%d]*(%s))",Nlinear,left.Data(),Nlinear+1,right.Data());
      formula.ReplaceAll(pattern,replacement);
      Nlinear += 2;
      TFormula *lin1 = new TFormula("__linear1",left);
      TFormula *lin2 = new TFormula("__linear2",right);
      lin1->SetBit(kNotGlobal,1);
      lin2->SetBit(kNotGlobal,1);
      gROOT->GetListOfFunctions()->Remove(lin1);
      gROOT->GetListOfFunctions()->Remove(lin2);
      fLinearParts.Add(lin1);
      fLinearParts.Add(lin2);

      linPos = formula.Index("@");
   }
}

void TFormula::PreProcessFormula(TString &formula)
{
   //*-*    
   //*-*    Preprocessing of formula
   //*-*    Replace all ** by ^, and removes spaces.
   //*-*    Handle also parametrized functions like polN,gaus,expo,landau
   //*-*    and exponentiation.
   //*-*    Similar functionality should be added here.
   //*-*    
   formula.ReplaceAll("**","^");
   formula.ReplaceAll(" ",""); 
   HandlePolN(formula);
   HandleParametrizedFunctions(formula);
   HandleExponentiation(formula);
   HandleLinear(formula);
}
Bool_t TFormula::PrepareFormula(TString &formula)
{
   fFuncs.clear();
   fReadyToExecute = false;
   ExtractFunctors(formula);
   fFuncs.sort();
   fFuncs.unique();

   ProcessFormula(formula);
   return fReadyToExecute;
}
void TFormula::ExtractFunctors(TString &formula)
{
   //*-*    
   //*-*    Extracts functors from formula, and put them in fFuncs.
   //*-*    Simple grammar:
   //*-*    <function>  := name(arg1,arg2...)
   //*-*    <variable>  := name
   //*-*    <parameter> := [number]
   //*-*    <name>      := String containing lower and upper letters, numbers, underscores
   //*-*    <number>    := Integer number
   //*-*    Operators are omitted.
   //*-*    
   TString name = "";
   TString body = "";
   for(Int_t i = 0 ; i < formula.Length(); ++i )
   {
      if(formula[i] == '[')
      {
         Int_t tmp = i;
         i++;
         TString param = "";
         while(formula[i] != ']' && i < formula.Length())
         {
            param.Append(formula[i++]);
         }
         i++;

         DoAddParameter(param,0,false);
         TString replacement = TString::Format("{[%s]}",param.Data());
         formula.Replace(tmp,i - tmp, replacement,replacement.Length());
         fFuncs.push_back(TFormulaFunction(param));
         continue;
      }
      if(isalpha(formula[i]) && !IsOperator(formula[i])) 
      {
         while( IsFunctionNameChar(formula[i]) && i < formula.Length())
         {
            name.Append(formula[i++]);
         }
         if(formula[i] == '(')
         {
            i++;
            if(formula[i] == ')')
            {
               fFuncs.push_back(TFormulaFunction(name,body,0));
               name = body = "";
               continue;  
            }
            Int_t depth = 1;
            Int_t args  = 1; // we will miss first argument
            while(depth != 0 && i < formula.Length())
            {
               switch(formula[i])
               {
                  case '(': depth++;   break;
                  case ')': depth--;   break;
                  case ',': if(depth == 1) args++; break;
               }
               if(depth != 0) // we don't want last ')' inside body
               {
                  body.Append(formula[i++]);
               }
            }
            Int_t originalBodyLen = body.Length();
            ExtractFunctors(body);
            formula.Replace(i-originalBodyLen,originalBodyLen,body,body.Length());
            i += body.Length() - originalBodyLen;
            fFuncs.push_back(TFormulaFunction(name,body,args));         
         }
         else
         {
            TString replacement = TString::Format("{%s}",name.Data());
            formula.Replace(i-name.Length(),name.Length(),replacement,replacement.Length());
            i += 2;
            fFuncs.push_back(TFormulaFunction(name));
         }
      }
      name = body = "";
      
   }
}
void TFormula::ProcessFormula(TString &formula)
{
   //*-*    
   //*-*    Iterates through funtors in fFuncs and performs the appropriate action.
   //*-*    If functor has 0 arguments (has only name) can be:
   //*-*     - variable
   //*-*       * will be replaced with x[num], where x is an array containing value of this variable under num.
   //*-*     - pre-defined formula
   //*-*       * will be replaced with formulas body
   //*-*     - constant
   //*-*       * will be replaced with constant value
   //*-*     - parameter
   //*-*       * will be replaced with p[num], where p is an array containing value of this parameter under num.
   //*-*    If has arguments it can be :
   //*-*     - function shortcut, eg. sin
   //*-*       * will be replaced with fullname of function, eg. sin -> TMath::Sin
   //*-*     - function from cling environment, eg. TMath::BreitWigner(x,y,z)
   //*-*       * first check if function exists, and has same number of arguments, then accept it and set as found.
   //*-*    If all functors after iteration are matched with corresponding action,
   //*-*    it inputs C++ code of formula into cling, and sets flag that formula is ready to evaluate.
   //*-*    

   //std::cout << "Begin: formula is " << formula << std::endl;

   for(list<TFormulaFunction>::iterator funcsIt = fFuncs.begin(); funcsIt != fFuncs.end(); ++funcsIt)
   {
      TFormulaFunction & fun = *funcsIt;
      //std::cout << "fun is " << fun.GetName() << std::endl;
      if(fun.fFound)
         continue;
      if(fun.IsFuncCall())
      {
         map<TString,TString>::iterator it = fFunctionsShortcuts.find(fun.GetName());
         if(it != fFunctionsShortcuts.end())
         {
            TString shortcut = it->first;
            TString full = it->second;
            formula.ReplaceAll(shortcut,full);
            fun.fFound = true;
         }
         if(fun.fName.Contains("::")) // add support for nested namespaces
         {
            // look for last occurence of "::"
            std::string name(fun.fName); 
            size_t index = name.rfind("::");
            assert(index != std::string::npos);               
            TString className = fun.fName(0,fun.fName(0,index).Length());           
            TString functionName = fun.fName(index + 2, fun.fName.Length());
                                                   
            Bool_t silent = true;
            TClass *tclass = new TClass(className,silent);
            const TList *methodList = tclass->GetListOfAllPublicMethods();
            TIter next(methodList);
            TMethod *p;
            while ((p = (TMethod*) next()))
            {
               if (strcmp(p->GetName(),functionName.Data()) == 0 &&
                   p->GetNargs() == fun.GetNargs())
               { 
                  fun.fFound = true;
                  break;
               }
            }
         }
         if(!fun.fFound)
         {
            Error("TFormula","Could not find %s function with %d argument(s)",fun.GetName(),fun.GetNargs());
         }
      }
      else
      {
         TFormula *old = (TFormula*)gROOT->GetListOfFunctions()->FindObject(gNamePrefix + fun.fName);
         if(old)
         {
            fun.fFound = true;
            TString pattern = TString::Format("{%s}",fun.GetName());
            TString replacement = old->GetExpFormula();
            PreProcessFormula(replacement);
            ExtractFunctors(replacement);
            formula.ReplaceAll(pattern,replacement);
            continue;
         }
         // looking for default variables defined in fVars
         map<TString,TFormulaVariable>::iterator varsIt = fVars.find(fun.GetName());
         if(varsIt!= fVars.end()) 
         {
            TString name = (*varsIt).second.GetName();
            Double_t value = (*varsIt).second.fValue;
            AddVariable(name,value); // this set the cling variable
            if(!fVars[name].fFound)
            {
               fVars[name].fFound = true;
               int varDim =  (*varsIt).second.fArrayPos;  // variable dimenions (0 for x, 1 for y, 2, for z)
               if (varDim >= fNdim) { 
                  fNdim = varDim+1;
                  // we need to be sure that all other variables are added with position less 
                  for ( auto &v : fVars) { 
                     if (v.second.fArrayPos < varDim && !v.second.fFound ) {                         
                        AddVariable(v.first, v.second.fValue);
                        v.second.fFound = true; 
                     }
                  }
               } 
            }
            // remove the "{.. }" added around the variable
            TString pattern = TString::Format("{%s}",name.Data());   
            TString replacement = TString::Format("x[%d]",(*varsIt).second.fArrayPos);
            formula.ReplaceAll(pattern,replacement);
              
            fun.fFound = true; 
            continue;          
         }
         // check for observables defined as x[0],x[1],....
         // maybe could use a regular expression here
         // only in case match with defined variables is not successfull
         TString funname = fun.GetName(); 
         if (funname.Contains("x[") && funname.Contains("]") ) { 
            TString sdigit = funname(2,funname.Index("]") );
            int digit = sdigit.Atoi(); 
            if (digit >= fNdim) { 
               fNdim = digit+1;
               // we need to add the variables in fVars all of them before x[n]
               for (int j = 0; j < fNdim; ++j) { 
                  TString vname = TString::Format("x[%d]",j);
                     if (fVars.find(vname) == fVars.end() ) { 
                        fVars[vname] = TFormulaVariable(vname,0,j);  
                        fVars[vname].fFound = true;
                        AddVariable(vname,0.);
                     }
               }
            }
            //std::cout << "Found matching for " << funname  << std::endl;
            fun.fFound = true; 
            // remove the "{.. }" added around the variable
            TString pattern = TString::Format("{%s}",funname.Data());   
            formula.ReplaceAll(pattern,funname);
            continue;
         } 
         //}


         map<TString,Double_t>::iterator constIt = fConsts.find(fun.GetName());
         if(constIt != fConsts.end())
         {
            TString pattern = TString::Format("{%s}",fun.GetName());
            TString value = TString::Format("%lf",(*constIt).second);
            formula.ReplaceAll(pattern,value);
            fun.fFound = true;
            continue;
         }
         
         map<TString,TFormulaVariable>::iterator paramsIt = fParams.find(fun.GetName());
         if(paramsIt != fParams.end())
         {
            TString name = (*paramsIt).second.GetName();
            TString pattern = TString::Format("{[%s]}",fun.GetName());
            //std::cout << "pattern is " << pattern << std::endl;
            if(formula.Index(pattern) != kNPOS)
            {
               TString replacement = TString::Format("p[%d]",(*paramsIt).second.fArrayPos);
               //std::cout << "replace pattern  " << pattern << " with " << replacement << std::endl;
               formula.ReplaceAll(pattern,replacement);
               
            }
            fun.fFound = true;
            continue;
         }
         fun.fFound = false;
      }
   }
   //std::cout << "End: formula is " << formula << std::endl;
   // check that all formula components arematched otherwise emit an error
   Bool_t allFunctorsMatched = true;
   for(list<TFormulaFunction>::iterator it = fFuncs.begin(); it != fFuncs.end(); it++)
   {
      if(!it->fFound)
      {
         allFunctorsMatched = false;
         Warning("ProcessFormula","\"%s\" has not been matched in the formula expression",it->GetName() );
         break;
      }
   }
   
   if(!fReadyToExecute && allFunctorsMatched)
   {
      fReadyToExecute = true;
      Bool_t hasVariables = (fNdim > 0);
      Bool_t hasParameters = (fNpar > 0);
      if(!hasParameters)
      {
         fAllParametersSetted = true;
      }
      // assume a function without variables is always 1-dimensional
      if (hasParameters && ! hasVariables) { 
         fNdim = 1; 
         AddVariable("x",0);
         hasVariables = true; 
      }
      Bool_t hasBoth = hasVariables && hasParameters;
      Bool_t inputIntoCling = (formula.Length() > 0);
      TString argumentsPrototype = 
         TString::Format("%s%s%s",(hasVariables ? "Double_t *x" : ""), (hasBoth ? "," : ""),
                        (hasParameters  ? "Double_t *p" : ""));
      // add also pointer to function name to make it unique
      fClingName = fName; 
      fClingName.ReplaceAll(" ",""); // remove also white space from function name;
      // hack for function names created with ++ in doing linear fitter. In this case use a different name
      // shuld make to all the case where special operator character are use din the name 
      if (fClingName.Contains("++") ) fClingName = "T__linearFunction";
      fClingName = TString::Format("%s_%p",fClingName.Data(),  this);

      fClingInput = TString::Format("Double_t %s(%s){ return %s ; }", fClingName.Data(),argumentsPrototype.Data(),formula.Data());

      //std::cout << "cling input " << fClingInput << std::endl;

      if(inputIntoCling)
      {
         InputFormulaIntoCling();
      }
      else
      {
         fReadyToExecute = true;
         fAllParametersSetted = true;
         fClingInitialized = true;
      }

   }
   // clean up un-used default variables 
   auto itvar = fVars.begin(); 
   do
   {
      if ( ! itvar->second.fFound ) {
         //std::cout << "Erase variable " << itvar->first << std::endl;
         itvar = fVars.erase(itvar);
      }
      else 
         itvar++;
   }
   while( itvar != fVars.end() ); 

}
const TObject* TFormula::GetLinearPart(Int_t i)
{
   // Return linear part.

   if (!fLinearParts.IsEmpty())
      return fLinearParts.UncheckedAt(i);
   return 0;
}
void TFormula::AddVariable(const TString &name, Double_t value)
{
   //*-*    
   //*-*    Adds variable to known variables, and reprocess formula.
   //*-*    
   
   if(fVars.find(name) != fVars.end() )
   {
      TFormulaVariable & var = fVars[name];
      var.fValue = value;
      
      // If the position is not defined in the Cling vectors, make space for it 
      // but normally is variable is defined in fVars a slot should be also present in fClingVariables
      if(var.fArrayPos < 0)
      {
         var.fArrayPos = fVars.size();
      }
      if(var.fArrayPos >= (int)fClingVariables.size())
      {
         fClingVariables.resize(var.fArrayPos+1);
      }
      fClingVariables[var.fArrayPos] = value;
   }
   else
   {
      TFormulaVariable var(name,value,fVars.size());
      fVars[name] = var;
      fClingVariables.push_back(value);
      ProcessFormula(fClingInput);
   }

}
void TFormula::AddVariables(const pair<TString,Double_t> *vars, const Int_t size)
{
   //*-*    
   //*-*    Adds multiple variables.
   //*-*    First argument is an array of pairs<TString,Double>, where
   //*-*    first argument is name of variable,
   //*-*    second argument represents value.
   //*-*    size - number of variables passed in first argument
   //*-*    

   Bool_t anyNewVar = false;
   for(Int_t i = 0 ; i < size; ++i)
   {

      pair<TString,Double_t> v = vars[i];

      TFormulaVariable &var = fVars[v.first];
      if(var.fArrayPos < 0)
      {

         var.fName = v.first;
         var.fArrayPos = fVars.size();
         anyNewVar = true;
         var.fValue = v.second;
         if(var.fArrayPos >= (int)fClingVariables.capacity())
         {
            Int_t multiplier = 2;
            if(fFuncs.size() > 100)
            {
               multiplier = TMath::Floor(TMath::Log10(fFuncs.size()) * 10);
            }
            fClingVariables.reserve(multiplier * fClingVariables.capacity());
         }
         fClingVariables.push_back(v.second);
      }
      else
      {
         var.fValue = v.second;
         fClingVariables[var.fArrayPos] = v.second;
      }
   }
   if(anyNewVar)
   {
      ProcessFormula(fClingInput);
   }

}
void TFormula::SetVariables(const pair<TString,Double_t> *vars, const Int_t size)
{
   //*-*    
   //*-*    Sets multiple variables.
   //*-*    First argument is an array of pairs<TString,Double>, where
   //*-*    first argument is name of variable,
   //*-*    second argument represents value.
   //*-*    size - number of variables passed in first argument
   //*-*
   for(Int_t i = 0; i < size; ++i)
   {
      pair<TString,Double_t> v = vars[i];
      if(fVars.find(v.first) != fVars.end())
      {
         fVars[v.first].fValue = v.second;
         fClingVariables[fVars[v.first].fArrayPos] = v.second;
      }  
      else
      {
         Error("SetVariables","Variable %s is not defined.",v.first.Data());
      }
   }
}

Double_t TFormula::GetVariable(const TString &name)
{
   //*-*    
   //*-*    Returns variable value.
   //*-*    
   if(fVars.find(name) == fVars.end())
   {
      Error("GetVariable","Variable %s is not defined.",name.Data());
      return -1;
   }
   return fVars[name].fValue;
}
void TFormula::SetVariable(const TString &name, Double_t value)
{
   //*-*    
   //*-*    Sets variable value.
   //*-*    
   if(fVars.find(name) == fVars.end())
   {
      Error("SetVariable","Variable %s is not defined.",name.Data());
      return;
   }
   fVars[name].fValue = value;
   fClingVariables[fVars[name].fArrayPos] = value;
}

void TFormula::DoAddParameter(const TString &name, Double_t value, Bool_t processFormula)
{
   //*-*    
   //*-*    Adds parameter to known parameters.
   //*-*    User should use SetParameter, because parameters are added during initialization part,
   //*-*    and after that adding new will be pointless.
   //*-*    

   //std::cout << "adding parameter " << name << std::endl;

   if(fParams.find(name) != fParams.end() )
   {
      TFormulaVariable & par = fParams[name];
      par.fValue = value;
      if (par.fArrayPos < 0)
      {
         par.fArrayPos = fParams.size();
      }
      if(par.fArrayPos >= (int)fClingParameters.capacity())
      {
         fClingParameters.reserve(2 * fClingParameters.capacity());
      }
      fClingParameters[par.fArrayPos] = value;
   }
   else
   {
      fNpar++;
      //TFormulaVariable(name,value,fParams.size());
      int pos = fParams.size(); 
      fParams.insert(pair<TString,TFormulaVariable>(name,TFormulaVariable(name,value,pos)));
      fClingParameters.push_back(value);
      if (processFormula) { 
         // replace first in input parameter name with [name]
         fClingInput.ReplaceAll(name,TString::Format("[%s]",name.Data() ) );
         ProcessFormula(fClingInput);
      }
   }

} 
Double_t TFormula::GetParameter(const TString &name)
{
   //*-*    
   //*-*    Returns parameter value given by string.
   //*-*    
   if(fParams.find(name) == fParams.end())
   {
      Error("GetParameter","Parameter %s is not defined.",name.Data());
      return -1;
   }
   return fClingInitialized ? fClingParameters[fParams[name].fArrayPos] : fParams[name].fValue;
}
Double_t TFormula::GetParameter(Int_t param)
{
   //*-*    
   //*-*    Return parameter value given by integer.
   //*-*    
   //*-*    
   TString name = TString::Format("%d",param);
   return GetParameter(name);
}
const char * TFormula::GetParName(Int_t ipar) const
{
   //*-*    
   //*-*    Return parameter name given by integer.
   //*-*    
   TString name = TString::Format("%d",ipar);
   map<TString,TFormulaVariable>::const_iterator it = fParams.find(name);
   if(it == fParams.end())
   {
      Error("GetParName","Parameter %s is not defined.",name.Data());
      return 0;
   }
   return it->second.GetName();
}
Double_t* TFormula::GetParameters() const
{
   if(!fClingParameters.empty())
      return const_cast<Double_t*>(&fClingParameters[0]); 
   return 0;
}

void TFormula::GetParameters(Double_t *params)
{
   for(Int_t i = 0; i < fNpar; ++i)
   {
      if (Int_t(fClingParameters.size()) > i) 
         params[i] = fClingParameters[i];
      else 
         params[i] = -1;
   }
}
void TFormula::SetParameter(const TString &name, Double_t value)
{
   //*-*    
   //*-*    Sets parameter value.
   //*-*    
   if(fParams.find(name) == fParams.end())
   {
      Error("SetParameter","Parameter %s is not defined.",name.Data());
      return;
   }
   fParams[name].fValue = value;
   fParams[name].fFound = true;
   fClingParameters[fParams[name].fArrayPos] = value;
   fAllParametersSetted = true;
   for(map<TString,TFormulaVariable>::iterator it = fParams.begin(); it != fParams.end(); it++)
   {
      if(!it->second.fFound)
      {
         fAllParametersSetted = false;
         break;
      }
   }
}
void TFormula::SetParameters(const pair<TString,Double_t> *params,const Int_t size)
{
   //*-*    
   //*-*    Set multiple parameters.
   //*-*    First argument is an array of pairs<TString,Double>, where
   //*-*    first argument is name of parameter,
   //*-*    second argument represents value.
   //*-*    size - number of params passed in first argument
   //*-*    
   for(Int_t i = 0 ; i < size ; ++i)
   {
      pair<TString,Double_t> p = params[i];
      if(fParams.find(p.first) == fParams.end())
      {
         Error("SetParameters","Parameter %s is not defined",p.first.Data());
         continue;
      }
      fParams[p.first].fValue = p.second;
      fParams[p.first].fFound = true;
      fClingParameters[fParams[p.first].fArrayPos] = p.second;
   }
   fAllParametersSetted = true;
   for(map<TString,TFormulaVariable>::iterator it = fParams.begin(); it != fParams.end(); it++)
   {
      if(!it->second.fFound)
      {
         fAllParametersSetted = false;
         break;
      }
   }
}
void TFormula::SetParameters(const Double_t *params)
{
   SetParameters(params,fNpar);
}
void TFormula::SetParameters(Double_t p0,Double_t p1,Double_t p2,Double_t p3,Double_t p4,
                   Double_t p5,Double_t p6,Double_t p7,Double_t p8,
                   Double_t p9,Double_t p10)
{
   if(fNpar >= 1) SetParameter(0,p0);
   if(fNpar >= 2) SetParameter(1,p1);
   if(fNpar >= 3) SetParameter(2,p2);
   if(fNpar >= 4) SetParameter(3,p3);
   if(fNpar >= 5) SetParameter(4,p4);
   if(fNpar >= 6) SetParameter(5,p5);
   if(fNpar >= 7) SetParameter(6,p6);
   if(fNpar >= 8) SetParameter(7,p7);
   if(fNpar >= 9) SetParameter(8,p8);
   if(fNpar >= 10) SetParameter(9,p9);
   if(fNpar >= 11) SetParameter(10,p10);
}
void TFormula::SetParameter(Int_t param, Double_t value)
{
   TString name = TString::Format("%d",param);
   SetParameter(name,value);
}
void TFormula::SetParNames(const char *name0,const char *name1,const char *name2,const char *name3,
                 const char *name4, const char *name5,const char *name6,const char *name7,
                 const char *name8,const char *name9,const char *name10)
{
   SetParName(0,name0);
   SetParName(1,name1);
   SetParName(2,name2);
   SetParName(3,name3);
   SetParName(4,name4);
   SetParName(5,name5);
   SetParName(6,name6);
   SetParName(7,name7);
   SetParName(8,name8);
   SetParName(9,name9);
   SetParName(10,name10);
}
void TFormula::SetParName(Int_t ipar, const char * name)
{
   Bool_t found = false;
   TString curName = TString::Format("%d",ipar);
   for(list<TFormulaFunction>::iterator it = fFuncs.begin(); it != fFuncs.end(); ++it)
   {
      if(curName == it->GetName())
      {
         found = true;
         it->fName = name;
         break;
      }
   }
   if(!found)
   {
      Error("SetParName","Parameter %d is not defined.",ipar);
      return;
   }
   TString pattern = TString::Format("[%d]",ipar);
   TString replacement = TString::Format("[%s]",name);
   fFormula.ReplaceAll(pattern,replacement);

   map<TString,TFormulaVariable>::iterator it = fParams.find(curName);
   TFormulaVariable copy = it->second;
   copy.fName = name;
   fParams.erase(it);
   fParams[name] = copy;

}
void TFormula::SetParameters(const Double_t *params, Int_t size)
{
   if(!params || size < 0 || size > fNpar) return;
   for(Int_t i = 0; i < size; ++i)
   {
      TString name = TString::Format("%d",i);
      SetParameter(name,params[i]);
   }
}
Double_t TFormula::EvalPar(const Double_t *x,const Double_t *params)
{
   if (params) SetParameters(params, fNpar);

   if(fNdim >= 1) fClingVariables[0] = x[0];
   if(fNdim >= 2) fClingVariables[1] = x[1];
   if(fNdim >= 3) fClingVariables[2] = x[2];
   if(fNdim >= 4) fClingVariables[3] = x[3];

   // if(fNdim >= 1) SetVariable("x",x[1]);
   // if(fNdim >= 2) SetVariable("y",x[1]);
   // if(fNdim >= 3) SetVariable("z",x[2]);
   // if(fNdim >= 4) SetVariable("t",x[3]);

   return Eval();
}
Double_t TFormula::Eval(Double_t x, Double_t y, Double_t z, Double_t t)
{
   //*-*    
   //*-*    Sets first 4  variables (e.g. x, y, z, t) and evaluate formula.
   //*-*    
   if(fNdim >= 1) fClingVariables[0] = x;
   if(fNdim >= 2) fClingVariables[1] = y;
   if(fNdim >= 3) fClingVariables[2] = z;
   if(fNdim >= 4) fClingVariables[3] = t;
   return Eval();
}
Double_t TFormula::Eval(Double_t x, Double_t y , Double_t z)
{
   //*-*    
   //*-*    Sets first 3  variables (e.g. x, y, z) and evaluate formula.
   //*-*    

   if(fNdim >= 1) fClingVariables[0] = x;
   if(fNdim >= 2) fClingVariables[1] = y;
   if(fNdim >= 3) fClingVariables[2] = z;
   // if(fNdim >= 1) SetVariable("x",x);
   // if(fNdim >= 2) SetVariable("y",y);
   // if(fNdim >= 3) SetVariable("z",z);

   return Eval();
}
Double_t TFormula::Eval(Double_t x, Double_t y)
{
   //*-*    
   //*-*    Sets first 2  variables (e.g. x and y) and evaluate formula.
   //*-*    

   if(fNdim >= 1) fClingVariables[0] = x;
   if(fNdim >= 2) fClingVariables[1] = y;
   return Eval();
}
Double_t TFormula::Eval(Double_t x)
{
   //*-*    
   //*-*    Sets first variable (e.g. x) and evaluate formula.
   //*-*    

   if(fNdim >= 1) fClingVariables[0] = x;
   return Eval();
}
Double_t TFormula::Eval()
{
   //*-*    
   //*-*    Evaluate formula.
   //*-*    If formula is not ready to execute(missing parameters/variables), 
   //*-*    print these which are not known.
   //*-*    If parameter has default value, and has not been setted, appropriate warning is shown.
   //*-*    


   if(!fReadyToExecute)
   {
      Error("Eval","Formula not ready to execute. Missing parameters/variables");
      for(list<TFormulaFunction>::iterator it = fFuncs.begin(); it != fFuncs.end(); ++it)
      {
         TFormulaFunction fun = *it;
         if(!fun.fFound)
         {
            printf("%s is unknown.\n",fun.GetName());
         }
      }
      return -1;
   }
   // This is not needed (we can always use the default values)
   // if(!fAllParametersSetted)
   // {
   //    Warning("Eval","Not all parameters are setted.");
   //    for(map<TString,TFormulaVariable>::iterator it = fParams.begin(); it != fParams.end(); ++it)
   //    {
   //       pair<TString,TFormulaVariable> param = *it;
   //       if(!param.second.fFound)
   //       {
   //          printf("%s has default value %lf\n",param.first.Data(),param.second.GetInitialValue());
   //       }
   //    }  

   // }
   Double_t result = 0;
   void* args[2]; 
   double * vars = fClingVariables.data();
   args[0] = &vars; 
   if (fNpar <= 0) 
      (*fFuncPtr)(0, 1, args, &result);
   else {
      double * pars = fClingParameters.data();
      args[1] = &pars;
      (*fFuncPtr)(0, 2, args, &result);
   }
   return result;
}

//______________________________________________________________________________
void TFormula::Print(Option_t *option) const
{
   // print the formula and its attributes
   printf(" %20s : %s Ndim= %d, Npar= %d, Number= %d \n",GetName(),GetTitle(), fNdim,fNpar,fNumber);
   printf(" Formula expression: \n");
   printf("\t%s \n",fFormula.Data() );
   TString opt(option);
   opt.ToUpper();
   if (opt.Contains("V") ) { 
      if (fNdim > 0) {
         printf("List of  Variables: \n");
         for ( map<TString,TFormulaVariable>::const_iterator it = fVars.begin(); it != fVars.end(); ++it) { 
            printf(" %20s =  %10f \n",it->first.Data(), fClingVariables[it->second.GetArrayPos()] );
         }
      }
      if (fNpar > 0) {
         printf("List of  Parameters: \n");
         for ( map<TString,TFormulaVariable>::const_iterator it = fParams.begin(); it != fParams.end(); ++it) { 
            printf(" %20s =  %10f \n",it->first.Data(), fClingParameters[it->second.GetArrayPos()] );
         }
      }
      printf("Expression passed to Cling:\n");
      printf("\t%s\n",fClingInput.Data() );
   }
   if(!fReadyToExecute)
   {
      Warning("Print","Formula is not ready to execute. Missing parameters/variables");
      for(list<TFormulaFunction>::const_iterator it = fFuncs.begin(); it != fFuncs.end(); ++it)
      {
         TFormulaFunction fun = *it;
         if(!fun.fFound)
         {
            printf("%s is uknown.\n",fun.GetName());
         }
      }
   }
   if(!fAllParametersSetted)
   {
      Info("Print","Not all parameters are setted.");
      for(map<TString,TFormulaVariable>::const_iterator it = fParams.begin(); it != fParams.end(); ++it)
      {
         pair<TString,TFormulaVariable> param = *it;
         if(!param.second.fFound)
         {
            printf("%s has default value %lf\n",param.first.Data(),param.second.GetInitialValue());
         }
      }  

   }

} 
