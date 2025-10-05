#include "base/abc/abc.h"
#include "base/main/main.h"
#include "base/main/mainInt.h"
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include "stdint.h"
#include "iostream"

using namespace std;

static int Lsv_CommandPrintNodes(Abc_Frame_t* pAbc, int argc, char** argv);
static int Lsv_CommandPrintCut(Abc_Frame_t* pAbc, int argc, char** argv);


void init(Abc_Frame_t* pAbc) {
  Cmd_CommandAdd(pAbc, "LSV", "lsv_print_nodes", Lsv_CommandPrintNodes, 0);
  Cmd_CommandAdd(pAbc, "LSV", "lsv_printmocut", Lsv_CommandPrintCut, 0);
}

void destroy(Abc_Frame_t* pAbc) {}

Abc_FrameInitializer_t frame_initializer = {init, destroy};

struct PackageRegistrationManager {
  PackageRegistrationManager() { Abc_FrameAddInitializer(&frame_initializer); }
} lsvPackageRegistrationManager;

void Lsv_NtkPrintNodes(Abc_Ntk_t* pNtk) {
  Abc_Obj_t* pObj;
  int i;
  Abc_NtkForEachNode(pNtk, pObj, i) {
    printf("Object Id = %d, name = %s\n", Abc_ObjId(pObj), Abc_ObjName(pObj));
    Abc_Obj_t* pFanin;
    int j;
    Abc_ObjForEachFanin(pObj, pFanin, j) {
      printf("  Fanin-%d: Id = %d, name = %s\n", j, Abc_ObjId(pFanin),
             Abc_ObjName(pFanin));
    }
    if (Abc_NtkHasSop(pNtk)) {
      printf("The SOP of this node:\n%s", (char*)pObj->pData);
    }
  }
}

int Lsv_CommandPrintNodes(Abc_Frame_t* pAbc, int argc, char** argv) {
  Abc_Ntk_t* pNtk = Abc_FrameReadNtk(pAbc);
  int c;
  Extra_UtilGetoptReset();
  while ((c = Extra_UtilGetopt(argc, argv, "h")) != EOF) {
    switch (c) {
      case 'h':
        goto usage;
      default:
        goto usage;
    }
  }
  if (!pNtk) {
    Abc_Print(-1, "Empty network.\n");
    return 1;
  }
  Lsv_NtkPrintNodes(pNtk);
  return 0;

usage:
  Abc_Print(-2, "usage: lsv_print_nodes [-h]\n");
  Abc_Print(-2, "\t        prints the nodes in the network\n");
  Abc_Print(-2, "\t-h    : print the command usage\n");
  return 1;
}

// Command handler for "lsv_printmocut <k> <l>"
void cutfunction(Abc_Ntk_t* pNtk, int k, int l) {
    assert(Abc_NtkIsStrash(pNtk));  
    int nObj = Abc_NtkObjNumMax(pNtk);  
    vector< vector< vector<int> > > cutSets(nObj);

    // Initialize cut lists
    Abc_Obj_t* pObj;
    int i;
    Abc_NtkForEachPi(pNtk, pObj, i) {
        int id = Abc_ObjId(pObj);
        cutSets[id].push_back({ id });  // trivial cut for PI
    }
    // Constant 1 node (if exists)
    Abc_Obj_t* pConst1 = Abc_AigConst1(pNtk);
    if (pConst1 != NULL) {
        int constId = Abc_ObjId(pConst1);

        if (pConst1->Type == ABC_OBJ_CONST1) {
            cutSets[constId].push_back({ constId });
        }
    }

    // Gather all internal AND nodes in topological order (by level)
    vector<Abc_Obj_t*> nodes;
    Abc_NtkForEachNode(pNtk, pObj, i) {
        nodes.push_back(pObj);
    }
    sort(nodes.begin(), nodes.end(), [](Abc_Obj_t* a, Abc_Obj_t* b) {
        return Abc_ObjLevel(a) < Abc_ObjLevel(b);
    });

    // Compute cuts
    for (Abc_Obj_t* pNode : nodes) {
        int nodeId = Abc_ObjId(pNode);
        Abc_Obj_t* pFanin0 = Abc_ObjFanin0(pNode);
        Abc_Obj_t* pFanin1 = Abc_ObjFanin1(pNode);

        int id0 = Abc_ObjId(pFanin0);
        int id1 = Abc_ObjId(pFanin1);

        vector< vector<int> > newCuts;
        
        for (auto& cutA : cutSets[id0]) {
            for (auto& cutB : cutSets[id1]) {
                vector<int> mergeCut;
                mergeCut.reserve(cutA.size() + cutB.size());
                set_union(cutA.begin(), cutA.end(), cutB.begin(), cutB.end(), back_inserter(mergeCut));
                if ((int)mergeCut.size() > k) {
                    continue;  
                }
                newCuts.push_back(move(mergeCut));
            }
        }

        newCuts.push_back({ nodeId });


        sort(newCuts.begin(), newCuts.end());
        newCuts.erase(unique(newCuts.begin(), newCuts.end()), newCuts.end());


        sort(newCuts.begin(), newCuts.end(), [](const vector<int>& a, const vector<int>& b) {
            if (a.size() != b.size()) return a.size() < b.size();
            return a < b;  
        });
        vector<char> keep(newCuts.size(), 1);
        for (size_t a = 0; a < newCuts.size(); ++a) {
            if (!keep[a]) continue;
            for (size_t b = a + 1; b < newCuts.size(); ++b) {
                if (!keep[b]) continue;
                if (includes(newCuts[b].begin(), newCuts[b].end(), newCuts[a].begin(), newCuts[a].end())) {
                    keep[b] = 0;
                }
            }
        }
        cutSets[nodeId].clear();
        for (size_t idx = 0; idx < newCuts.size(); ++idx) {
            if (keep[idx]) {
                cutSets[nodeId].push_back(newCuts[idx]);
            }
        }
    }

    map< vector<int>, set<int> > cutToOutputs;
    Abc_NtkForEachNode(pNtk, pObj, i) {
        if (Abc_ObjIsPo(pObj)) continue;
        int objId = Abc_ObjId(pObj);
        for (auto& cut : cutSets[objId]) {
            cutToOutputs[cut].insert(objId);
        }
    }

    // Print cuts
    for (auto& entry : cutToOutputs) {
        const vector<int>& cut = entry.first;
        const set<int>& outs = entry.second;
        if ((int)outs.size() < l) continue;
        vector<int> outList(outs.begin(), outs.end());
        sort(outList.begin(), outList.end());
        for (size_t j = 0; j < cut.size(); ++j) {
            printf("%d", cut[j]);
            if (j < cut.size() - 1) printf(" ");
        }
        printf(" : ");
        for (size_t j = 0; j < outList.size(); ++j) {
            printf("%d", outList[j]);
            if (j < outList.size() - 1) printf(" ");
        }
        printf("\n");
    }
}

// Command handler for "lsv_printmocut <k> <l>" 
int Lsv_CommandPrintCut(Abc_Frame_t* pAbc, int argc, char** argv) {
    Abc_Ntk_t* pNtk = Abc_FrameReadNtk(pAbc);
    int c, k, l;
    Extra_UtilGetoptReset();
    // Parse optional flags (only -h for help)
    while ((c = Extra_UtilGetopt(argc, argv, "h")) != EOF) {
        switch (c) {
            case 'h':
            default:
                goto usage;
        }
    }
    if (pNtk == NULL) {
        Abc_Print(-1, "Empty network.\n");
        return 1;
    }

    // must be AIG
    if (!Abc_NtkIsStrash(pNtk)) {
        Abc_Print(-1, "The current network is not STRASH.\n");
        Abc_Print(-1, "Please run \"strash\" first.\n");
        return 1;
    }

    //detemine k and l
    if (argc != globalUtilOptind + 2) {
        Abc_Print(-1, "Wrong number of arguments.\n");
        goto usage;
    }
    k = atoi(argv[globalUtilOptind]);
    l = atoi(argv[globalUtilOptind + 1]);
    if (k < 3 || k > 6 || l < 1 || l > 4) {
        Abc_Print(-1, "Invalid values for k or l.\n");
        goto usage;
    }
    cutfunction(pNtk, k, l);
    return 0;

usage:
    Abc_Print(-2, "usage: lsv_printmocut <k> <l> [-h]\n");
    Abc_Print(-2, "\t         prints all k-l multi-output cuts of the current AIG network\n");
    Abc_Print(-2, "\t-h     : print the command usage\n");
    return 1;
}