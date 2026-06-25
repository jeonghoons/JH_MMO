#include "RecastNavMesh.h"

int main()
{
	RecastNavMesh navMesh;
	std::string binPath = "../MapResource/ParagonSample/Geometry/ParagonSample_Geo.bin";

	std::string navPath = "../ParagonSample_Geo.nav";
	if (navMesh.Build(binPath)) {
		std::cout << "\n=== NavMesh 빌드 성공! ===" << std::endl;

		// [핵심] 빌드가 성공하면 .nav 파일로 구워냅니다.
		if (navMesh.SaveNavMeshBinary(navPath)) {
			std::cout << "=== .nav파일 Load 완료! ===" << std::endl;
		}
	}
	else {
		std::cerr << "\n=== NavMesh 빌드 실패! ===" << std::endl;
	}

}