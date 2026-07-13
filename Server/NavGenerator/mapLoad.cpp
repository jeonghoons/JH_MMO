#include "RecastNavMesh.h"

int main()
{
	RecastNavMesh navMesh;
	std::string binPath = "../MapResource/Export/ParagonSample/Geometry/ParagonSample_Geo.bin";

	std::string navPath = "../Server/Resource/Map/ParagonSample_Geo.nav";
	if (navMesh.Build(binPath)) {
		std::cout << "\n=== NavMesh 빌드 성공! ===" << std::endl;

		if (navMesh.SaveNavMeshBinary(navPath)) {
			std::cout << "=== .nav파일 Load 완료! ===" << std::endl;
		}
	}
	else {
		std::cerr << "\n=== NavMesh 빌드 실패! ===" << std::endl;
	}

}