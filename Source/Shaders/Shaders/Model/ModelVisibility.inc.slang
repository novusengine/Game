#ifndef MODEL_VISIBILITY_INCLUDED
#define MODEL_VISIBILITY_INCLUDED

#include "Include/TypedVisibility.inc.slang"
#include "Model/ModelGeometry.inc.slang"
#include "Model/ModelViewWork.inc.slang"
#include "Model/ModelVisibilityRecord.inc.slang"

uint ModelVisibilityRecordCount(StructuredBuffer<uint> stats)
{
    return stats[MODEL_WORK_STAT_VISIBILITY_RECORDS];
}

#endif
