//===-- AtomiccMetaGen.cpp - Generating Verilog from LLVM -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements zzz
//
//===----------------------------------------------------------------------===//
#include "llvm/IR/Instructions.h"

using namespace llvm;

#include "AtomiccDecl.h"

typedef std::map<std::string,std::list<Value *>> MetaRef;
typedef struct {
    MetaRef list[MetaMax];
} MetaData;
static std::map<const Function *, MetaData> funcMetaMap;
static MetaData *baseMeta;

void appendList(int listIndex, BasicBlock *cond, std::string item)
{
    if (baseMeta && generateRegion == ProcessVerilog) {
        Value *val = getCondition(cond, 0);
        if (!val)
            baseMeta->list[listIndex][item].clear();
        for (auto condIter: baseMeta->list[listIndex][item])
             if (!condIter)
                 return;
             else if (condIter == val)
                 return;
        baseMeta->list[listIndex][item].push_back(val);
    }
}
static std::string gatherList(MetaData *bm, int listIndex)
{
    std::string temp;
    for (auto titem: bm->list[listIndex])
        for (auto item: titem.second)
            temp += printOperand(item,false) + ":" + titem.first + ";";
    return temp;
}

static std::string combineCondList(std::list<ReferenceType> &functionList)
{
    std::string temp, valsep;
    Value *prevCond = NULL;
    int remain = functionList.size();
    for (auto info: functionList) {
        remain--;
        temp += valsep;
        valsep = "";
        Value *opCond = getCondition(info.cond, 0);
        if (opCond && (remain || getCondition(info.cond, 1) != prevCond))
            temp += printOperand(opCond, false) + " ? ";
        temp += info.item;
        if (opCond && remain)
            valsep = " : ";
        prevCond = opCond;
    }
    return temp;
}

void metaPrepare(const StructType *STy)
{
    ClassMethodTable *table = classCreate[STy];
    for (auto FI : table->method) {
        Function *func = FI.second;
        baseMeta = &funcMetaMap[func];
        processFunction(func);
        if (!isActionMethod(func))
            table->guard[func] = combineCondList(functionList);
        else {
            table->storeList[func] = storeList;
            if (functionList.size() > 0) {
                printf("%s: non-store lines in Action\n", __FUNCTION__);
                func->dump();
                for (auto info: functionList) {
                    if (Value *cond = getCondition(info.cond, 0))
                        printf("%s\n", ("    if (" + printOperand(cond, false) + ")").c_str());
                    printf("%s\n", info.item.c_str());
                }
                exit(-1);
            }
        }
    }
    baseMeta = NULL;
}

void metaGenerate(const StructType *STy, FILE *OStr)
{
    ClassMethodTable *table = classCreate[STy];
    PrefixType interfacePrefix;
    buildPrefix(table, interfacePrefix);
    std::string name = getStructName(table->STy);
    std::map<std::string, int> exclusiveSeen;

    // write out metadata comments at end of the file
    table->metaList.push_front("//METASTART; " + name);
    int Idx = 0;
    for (auto I = STy->element_begin(), E = STy->element_end(); I != E; ++I, Idx++) {
        const Type *element = *I;
        int64_t vecCount = -1;
        int dimIndex = 0;
        std::string vecDim;
        if (Type *newType = table->replaceType[Idx]) {
            element = newType;
            vecCount = table->replaceCount[Idx];
        }
        do {
        std::string fname = fieldName(STy, Idx);
        if (fname != "") {
            if (vecCount != -1)
                fname += utostr(dimIndex++);
            if (const PointerType *PTy = dyn_cast<PointerType>(element)) {
                if (const StructType *STy = dyn_cast<StructType>(PTy->getElementType()))
                    table->metaList.push_back("//METAEXTERNAL; " + fname + "; " + getStructName(STy) + ";");
            }
            else if (const StructType *STy = dyn_cast<StructType>(element)) {
                std::string sname = getStructName(STy);
                if (sname.substr(0,12) != "l_struct_OC_")
                if (!inheritsModule(STy, "class.InterfaceClass") && !inheritsModule(STy, "class.BitsClass"))
                    table->metaList.push_back("//METAINTERNAL; " + fname + "; " + sname + ";");
            }
        }
        } while(vecCount-- > 0);
    }
    for (auto FI : table->method) {
        std::string mname = interfacePrefix[FI.first] + FI.first;
        Function *func = FI.second;
        std::string temp = table->guard[func];
        if (endswith(mname, "__RDY"))
            table->metaList.push_back("//METAGUARD; " + mname.substr(0, mname.length()-5) + "; " + temp + ";");
        else if (endswith(mname, "__READY"))
            table->metaList.push_back("//METAGUARDV; " + mname.substr(0, mname.length()-7) + "; " + temp + ";");
        else { // gather up metadata generated by processFunction
            MetaData *bm = &funcMetaMap[func];
            std::string temp = gatherList(bm, MetaInvoke);
            if (temp != "")
                table->metaList.push_back("//METAINVOKE; " + mname + "; " + temp);
            std::map<std::string,std::string> metaBefore;
            std::map<std::string,std::string> metaConflict;
            for (auto innerFI : table->method) {
                std::string innermname = interfacePrefix[innerFI.first] + innerFI.first;
                Function *innerfunc = innerFI.second;
                MetaData *innerbm = &funcMetaMap[innerfunc];
                std::string tempConflict;
                if (innerfunc == func)
                    continue;
                for (auto inneritem: innerbm->list[MetaWrite]) {
                    for (auto item: bm->list[MetaRead])
                        if (item.first == inneritem.first) {
                            metaBefore[innermname] = "; :";
                            break;
                        }
                    for (auto item: bm->list[MetaWrite])
                        if (item.first == inneritem.first) {
                            metaConflict[innermname] = "; ";
                            break;
                        }
                }
                for (auto inneritem: innerbm->list[MetaInvoke]) {
                    for (auto item: bm->list[MetaInvoke])
                        if (item.first == inneritem.first) {
//printf("[%s:%d] mname %s innermname %s item %s\n", __FUNCTION__, __LINE__, mname.c_str(), innermname.c_str(), item.first.c_str());
                            metaConflict[innermname] = "; ";
                            break;
                        }
                }
            }
            std::string metaStr;
            for (auto item: metaConflict)
                 if (item.second != "" && !exclusiveSeen[item.first])
                     metaStr += item.second + item.first;
            exclusiveSeen[mname] = 1;
            if (metaStr != "")
                table->metaList.push_back("//METAEXCLUSIVE; " + mname + metaStr);
            metaStr = "";
            for (auto item: metaBefore)
                 if (item.second != "")
                     metaStr += item.second + item.first;
            if (metaStr != "")
                table->metaList.push_back("//METABEFORE; " + mname + metaStr);
        }
    }
    std::string ruleNames;
    for (auto item : table->ruleFunctions)
        if (item.second)
            ruleNames += "; " + item.first;
    if (ruleNames != "")
        table->metaList.push_back("//METARULES" + ruleNames);
    for (auto item: table->interfaceConnect) {
        int Idx = 0;
        for (auto I = item.STy->element_begin(), E = item.STy->element_end(); I != E; ++I, Idx++) {
            std::string fname = fieldName(item.STy, Idx);
            if (endswith(fname, "__RDYp")) {
                fname = fname.substr(0, fname.length() - 6);
                std::string tname = item.target;
                for (unsigned i = 0; i < tname.length(); i++)
                    if (tname[i] == '.')
                        tname[i] = '$';
                std::string sname = item.source;
                for (unsigned i = 0; i < sname.length(); i++)
                    if (sname[i] == '.')
                        sname[i] = '$';
                table->metaList.push_back("//METACONNECT; " + tname + "$" + fname + "; " + sname + "$" + fname);
            }
        }
    }
    for (auto item : table->priority)
        table->metaList.push_back("//METAPRIORITY; " + item.first + "; " + item.second);
    for (auto item : table->metaList)
        fprintf(OStr, "%s\n", item.c_str());
}
