#include "RecastNavMesh.h"

int main(int argc, char** argv)
{
	std::string binPath;
	std::string navPath;

	if (argc >= 3) {
		// NavGenerator.exe <GeoBinPath> <NavOutputPath>
		binPath = argv[1];
		navPath = argv[2];
	}
	else {
		std::string mapName = (argc == 2) ? argv[1] : "ParagonSample";
		binPath = "../MapResource/Export/" + mapName + "/Geometry/" + mapName + "_Geo.bin";
		navPath = "../Server/Resource/Map/" + mapName + "_Geo.nav";
	}

	RecastNavMesh navMesh;
	if (navMesh.Build(binPath)) {
		std::cout << "\n=== NavMesh ���� ����! ===" << std::endl;

		if (navMesh.SaveNavMeshBinary(navPath)) {
			std::cout << "=== .nav���� Load �Ϸ�! ===" << std::endl;
		}
	}
	else {
		std::cerr << "\n=== NavMesh ���� ����! ===" << std::endl;
	}

}