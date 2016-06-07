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

static int inhibitAppend;
void appendList(int listIndex, BasicBlock *cond, std::string item)
{
    if (baseMeta && !inhibitAppend && generateRegion == ProcessVerilog) {
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

void startMeta(const Function *func)
{
    baseMeta = &funcMetaMap[func];
}

void metaGenerate(FILE *OStr, ClassMethodTable *table, PrefixType &interfacePrefix)
{
    std::list<std::string> metaList;
    std::string name = getStructName(table->STy);
    std::map<std::string, int> exclusiveSeen;
    // write out metadata comments at end of the file
    inhibitAppend = 1;
    metaList.push_back("//METASTART; " + name);
    for (auto FI : table->method) {
        std::string mname = interfacePrefix[FI.first] + FI.first;
        Function *func = FI.second;
        std::string temp = table->guard[func];
        if (endswith(mname, "__RDY"))
            metaList.push_back("//METAGUARD; " + mname.substr(0, mname.length()-5) + "; " + temp + ";");
        else if (endswith(mname, "__READY"))
            metaList.push_back("//METAGUARDV; " + mname.substr(0, mname.length()-7) + "; " + temp + ";");
        else { // gather up metadata generated by processFunction
            MetaData *bm = &funcMetaMap[func];
            std::string temp = gatherList(bm, MetaInvoke);
            if (temp != "")
                metaList.push_back("//METAINVOKE; " + mname + "; " + temp);
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
                metaList.push_back("//METAEXCLUSIVE; " + mname + metaStr);
            metaStr = "";
            for (auto item: metaBefore)
                 if (item.second != "")
                     metaStr += item.second + item.first;
            if (metaStr != "")
                metaList.push_back("//METABEFORE; " + mname + metaStr);
        }
    }
    std::string ruleNames;
    for (auto item : table->ruleFunctions)
        if (item.second)
            ruleNames += "; " + item.first;
    if (ruleNames != "")
        metaList.push_back("//METARULES" + ruleNames);
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
                metaList.push_back("//METACONNECT; " + tname + "$" + fname + "; " + sname + "$" + fname);
            }
        }
    }
    for (auto item : table->priority)
        metaList.push_back("//METAPRIORITY; " + item.first + "; " + item.second);
    for (auto item : metaList)
        fprintf(OStr, "%s\n", item.c_str());
    inhibitAppend = 0;
}
