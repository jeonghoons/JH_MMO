-- 2026-10-28: Item_Meta/Player_Inventory/Trade_Log, Player_Info.Mp, User_Account.Status 삭제 반영 (migrations/0001_2026-10-28_drop_unused.sql)
-- 2026-11-07: 스키마 변경 없음. Player_Info.PlayerType = 체형(1 MALE, 2 FEMALE)만, Player_Equipped.EquipSlot = Protocol::EquipPart(1~6), 가입 시 기본 장비 6행 (migrations/0002_2026-11-07_mc33_equip.sql — 기존 DB의 옛 값 정리)
-- 2026-11-16: Schema_Version(적용한 마이그레이션 기록, 서버가 시작할 때 DB_SCHEMA_VERSION과 비교), Player_Info.UpdatedAt, Player_Info CHECK 제약 (migrations/0003_2026-11-16_a25_schema_version.sql)
-- 2026-11-16: Player_Item(아이템 인스턴스 — 인벤토리·장착·경매 보관·수령함) 추가, Player_Equipped 삭제 (migrations/0004_2026-11-16_a33_player_item.sql)
-- 2026-11-16: Player_Item.BagSlot(인벤토리 칸 번호) + UX_Player_Item_BagSlot 추가 (migrations/0005_2026-11-16_a61_bag_slot.sql)
-- 이 파일은 새 DB 생성용이다. 기존 DB는 migrate.ps1로 올린다. 스키마를 바꾸면 마이그레이션 + 이 파일 + Server/DB/DbQueries.h의 DB_SCHEMA_VERSION을 함께 고친다.
USE [master]
GO
/****** 개체: Database [GameServer] 스크립트 날짜: 2026-09-25 오후 4:44:56 ******/
CREATE DATABASE [GameServer]
 CONTAINMENT = NONE
 ON  PRIMARY 
( NAME = N'GameServer', FILENAME = N'C:\Program Files\Microsoft SQL Server\MSSQL17.SQLEXPRESS\MSSQL\DATA\GameServer.mdf' , SIZE = 8192KB , MAXSIZE = UNLIMITED, FILEGROWTH = 65536KB )
 LOG ON 
( NAME = N'GameServer_log', FILENAME = N'C:\Program Files\Microsoft SQL Server\MSSQL17.SQLEXPRESS\MSSQL\DATA\GameServer_log.ldf' , SIZE = 8192KB , MAXSIZE = 2048GB , FILEGROWTH = 65536KB )
 WITH CATALOG_COLLATION = DATABASE_DEFAULT, LEDGER = OFF
GO
ALTER DATABASE [GameServer] SET COMPATIBILITY_LEVEL = 170
GO
IF (1 = FULLTEXTSERVICEPROPERTY('IsFullTextInstalled'))
begin
EXEC [GameServer].[dbo].[sp_fulltext_database] @action = 'enable'
end
GO
ALTER DATABASE [GameServer] SET ANSI_NULL_DEFAULT OFF 
GO
ALTER DATABASE [GameServer] SET ANSI_NULLS OFF 
GO
ALTER DATABASE [GameServer] SET ANSI_PADDING OFF 
GO
ALTER DATABASE [GameServer] SET ANSI_WARNINGS OFF 
GO
ALTER DATABASE [GameServer] SET ARITHABORT OFF 
GO
ALTER DATABASE [GameServer] SET AUTO_CLOSE OFF 
GO
ALTER DATABASE [GameServer] SET AUTO_SHRINK OFF 
GO
ALTER DATABASE [GameServer] SET AUTO_UPDATE_STATISTICS ON 
GO
ALTER DATABASE [GameServer] SET CURSOR_CLOSE_ON_COMMIT OFF 
GO
ALTER DATABASE [GameServer] SET CURSOR_DEFAULT  GLOBAL 
GO
ALTER DATABASE [GameServer] SET CONCAT_NULL_YIELDS_NULL OFF 
GO
ALTER DATABASE [GameServer] SET NUMERIC_ROUNDABORT OFF 
GO
ALTER DATABASE [GameServer] SET QUOTED_IDENTIFIER OFF 
GO
ALTER DATABASE [GameServer] SET RECURSIVE_TRIGGERS OFF 
GO
ALTER DATABASE [GameServer] SET  DISABLE_BROKER 
GO
ALTER DATABASE [GameServer] SET AUTO_UPDATE_STATISTICS_ASYNC OFF 
GO
ALTER DATABASE [GameServer] SET DATE_CORRELATION_OPTIMIZATION OFF 
GO
ALTER DATABASE [GameServer] SET TRUSTWORTHY OFF 
GO
ALTER DATABASE [GameServer] SET ALLOW_SNAPSHOT_ISOLATION OFF 
GO
ALTER DATABASE [GameServer] SET PARAMETERIZATION SIMPLE 
GO
ALTER DATABASE [GameServer] SET READ_COMMITTED_SNAPSHOT OFF 
GO
ALTER DATABASE [GameServer] SET HONOR_BROKER_PRIORITY OFF 
GO
ALTER DATABASE [GameServer] SET RECOVERY SIMPLE 
GO
ALTER DATABASE [GameServer] SET  MULTI_USER 
GO
ALTER DATABASE [GameServer] SET PAGE_VERIFY CHECKSUM  
GO
ALTER DATABASE [GameServer] SET DB_CHAINING OFF 
GO
ALTER DATABASE [GameServer] SET FILESTREAM( NON_TRANSACTED_ACCESS = OFF ) 
GO
ALTER DATABASE [GameServer] SET TARGET_RECOVERY_TIME = 60 SECONDS 
GO
ALTER DATABASE [GameServer] SET DELAYED_DURABILITY = DISABLED 
GO
ALTER DATABASE [GameServer] SET OPTIMIZED_LOCKING = OFF 
GO
ALTER DATABASE [GameServer] SET ACCELERATED_DATABASE_RECOVERY = OFF  
GO
ALTER DATABASE [GameServer] SET QUERY_STORE = ON
GO
ALTER DATABASE [GameServer] SET QUERY_STORE (OPERATION_MODE = READ_WRITE, CLEANUP_POLICY = (STALE_QUERY_THRESHOLD_DAYS = 30), DATA_FLUSH_INTERVAL_SECONDS = 900, INTERVAL_LENGTH_MINUTES = 60, MAX_STORAGE_SIZE_MB = 1000, QUERY_CAPTURE_MODE = AUTO, SIZE_BASED_CLEANUP_MODE = AUTO, MAX_PLANS_PER_QUERY = 200, WAIT_STATS_CAPTURE_MODE = ON)
GO
USE [GameServer]
GO
/****** 개체: Table [dbo].[Player_Item] — 아이템 한 개(스택이면 한 묶음) = 한 행. Location 1 인벤토리 · 2 장착 · 3 경매 보관 · 4 수령함 ******/
SET ANSI_NULLS ON
GO
SET QUOTED_IDENTIFIER ON
GO
CREATE TABLE [dbo].[Player_Item](
	[ItemDbId] [bigint] IDENTITY(1,1) NOT NULL,
	[OwnerId] [bigint] NOT NULL,
	[ItemId] [int] NOT NULL,
	[Count] [int] NOT NULL,
	[Location] [tinyint] NOT NULL,
	[EquipSlot] [tinyint] NOT NULL CONSTRAINT [DF_Player_Item_EquipSlot] DEFAULT ((0)),
	[BagSlot] [int] NOT NULL CONSTRAINT [DF_Player_Item_BagSlot] DEFAULT ((-1)),
	[CreatedAt] [datetime2](0) NOT NULL CONSTRAINT [DF_Player_Item_CreatedAt] DEFAULT (sysdatetime()),
	[UpdatedAt] [datetime2](0) NOT NULL CONSTRAINT [DF_Player_Item_UpdatedAt] DEFAULT (sysdatetime()),
 CONSTRAINT [PK_Player_Item] PRIMARY KEY CLUSTERED ([ItemDbId] ASC)
) ON [PRIMARY]
GO
CREATE NONCLUSTERED INDEX [IX_Player_Item_Owner] ON [dbo].[Player_Item] ([OwnerId] ASC, [Location] ASC)
GO
-- 한 캐릭터의 같은 장착 슬롯에 두 행이 들어가지 못하게 한다. 필터 인덱스라 QUOTED_IDENTIFIER·ANSI_NULLS가 켜진 연결에서만 이 테이블에 쓸 수 있다.
CREATE UNIQUE NONCLUSTERED INDEX [UX_Player_Item_Equip] ON [dbo].[Player_Item] ([OwnerId] ASC, [EquipSlot] ASC) WHERE [Location] = 2
GO
-- 인벤토리(Location 1) 행의 칸 번호(0부터), 그 밖의 위치는 -1. 한 캐릭터의 같은 칸에 두 행이 들어가지 못하게 한다.
CREATE UNIQUE NONCLUSTERED INDEX [UX_Player_Item_BagSlot] ON [dbo].[Player_Item] ([OwnerId] ASC, [BagSlot] ASC) WHERE [Location] = 1
GO
/****** 개체: Table [dbo].[Player_Info] 스크립트 날짜: 2026-09-25 오후 4:44:56 ******/
SET ANSI_NULLS ON
GO
SET QUOTED_IDENTIFIER ON
GO
CREATE TABLE [dbo].[Player_Info](
	[PlayerId] [bigint] IDENTITY(1,1) NOT NULL,
	[AccountId] [bigint] NOT NULL,
	[Name] [nvarchar](20) NOT NULL,
	[PlayerType] [int] NOT NULL,
	[Level] [int] NOT NULL,
	[Exp] [bigint] NOT NULL,
	[Hp] [int] NOT NULL,
	[Gold] [bigint] NOT NULL,
	[CreateDate] [datetime] NOT NULL,
	[UpdatedAt] [datetime2](0) NULL,
 CONSTRAINT [PK_Player_Info] PRIMARY KEY CLUSTERED 
(
	[PlayerId] ASC
)WITH (PAD_INDEX = OFF, STATISTICS_NORECOMPUTE = OFF, IGNORE_DUP_KEY = OFF, ALLOW_ROW_LOCKS = ON, ALLOW_PAGE_LOCKS = ON, OPTIMIZE_FOR_SEQUENTIAL_KEY = OFF) ON [PRIMARY]
) ON [PRIMARY]
GO
/****** 개체: Table [dbo].[User_Account] 스크립트 날짜: 2026-09-25 오후 4:44:56 ******/
SET ANSI_NULLS ON
GO
SET QUOTED_IDENTIFIER ON
GO
CREATE TABLE [dbo].[User_Account](
	[AccountId] [bigint] IDENTITY(1,1) NOT NULL,
	[LoginId] [varchar](50) NOT NULL,
	[LoginPassword] [varchar](50) NOT NULL,
 CONSTRAINT [PK_User_Account] PRIMARY KEY CLUSTERED 
(
	[AccountId] ASC
)WITH (PAD_INDEX = OFF, STATISTICS_NORECOMPUTE = OFF, IGNORE_DUP_KEY = OFF, ALLOW_ROW_LOCKS = ON, ALLOW_PAGE_LOCKS = ON, OPTIMIZE_FOR_SEQUENTIAL_KEY = OFF) ON [PRIMARY]
) ON [PRIMARY]
GO
/****** 개체: Table [dbo].[User_Info] 스크립트 날짜: 2026-09-25 오후 4:44:56 ******/
SET ANSI_NULLS ON
GO
SET QUOTED_IDENTIFIER ON
GO
CREATE TABLE [dbo].[User_Info](
	[AccountId] [bigint] NOT NULL,
	[LastLoginAt] [datetime] NULL,
	[CreateDate] [datetime] NOT NULL,
 CONSTRAINT [PK_User_Info] PRIMARY KEY CLUSTERED 
(
	[AccountId] ASC
)WITH (PAD_INDEX = OFF, STATISTICS_NORECOMPUTE = OFF, IGNORE_DUP_KEY = OFF, ALLOW_ROW_LOCKS = ON, ALLOW_PAGE_LOCKS = ON, OPTIMIZE_FOR_SEQUENTIAL_KEY = OFF) ON [PRIMARY]
) ON [PRIMARY]
GO
/****** 개체: Index [IX_Player_Info_AccountDbId] 스크립트 날짜: 2026-09-25 오후 4:44:56 ******/
CREATE NONCLUSTERED INDEX [IX_Player_Info_AccountDbId] ON [dbo].[Player_Info]
(
	[AccountId] ASC
)WITH (PAD_INDEX = OFF, STATISTICS_NORECOMPUTE = OFF, SORT_IN_TEMPDB = OFF, DROP_EXISTING = OFF, ONLINE = OFF, ALLOW_ROW_LOCKS = ON, ALLOW_PAGE_LOCKS = ON, OPTIMIZE_FOR_SEQUENTIAL_KEY = OFF) ON [PRIMARY]
GO
SET ANSI_PADDING ON
GO
/****** 개체: Index [IX_Player_Info_Name] 스크립트 날짜: 2026-09-25 오후 4:44:56 ******/
CREATE UNIQUE NONCLUSTERED INDEX [IX_Player_Info_Name] ON [dbo].[Player_Info]
(
	[Name] ASC
)WITH (PAD_INDEX = OFF, STATISTICS_NORECOMPUTE = OFF, SORT_IN_TEMPDB = OFF, IGNORE_DUP_KEY = OFF, DROP_EXISTING = OFF, ONLINE = OFF, ALLOW_ROW_LOCKS = ON, ALLOW_PAGE_LOCKS = ON, OPTIMIZE_FOR_SEQUENTIAL_KEY = OFF) ON [PRIMARY]
GO
SET ANSI_PADDING ON
GO
/****** 개체: Index [IX_User_Account_LoginId] 스크립트 날짜: 2026-09-25 오후 4:44:56 ******/
CREATE UNIQUE NONCLUSTERED INDEX [IX_User_Account_LoginId] ON [dbo].[User_Account]
(
	[LoginId] ASC
)WITH (PAD_INDEX = OFF, STATISTICS_NORECOMPUTE = OFF, SORT_IN_TEMPDB = OFF, IGNORE_DUP_KEY = OFF, DROP_EXISTING = OFF, ONLINE = OFF, ALLOW_ROW_LOCKS = ON, ALLOW_PAGE_LOCKS = ON, OPTIMIZE_FOR_SEQUENTIAL_KEY = OFF) ON [PRIMARY]
GO
ALTER TABLE [dbo].[Player_Info] ADD  CONSTRAINT [DF_Player_Info_PlayerType]  DEFAULT ((0)) FOR [PlayerType]
GO
ALTER TABLE [dbo].[Player_Info] ADD  CONSTRAINT [DF__Player_In__Level__70DDC3D8]  DEFAULT ((1)) FOR [Level]
GO
ALTER TABLE [dbo].[Player_Info] ADD  CONSTRAINT [DF__Player_Info__Exp__71D1E811]  DEFAULT ((0)) FOR [Exp]
GO
ALTER TABLE [dbo].[Player_Info] ADD  CONSTRAINT [DF_Player_Info_Hp]  DEFAULT ((100)) FOR [Hp]
GO
ALTER TABLE [dbo].[Player_Info] ADD  CONSTRAINT [DF__Player_Inf__Gold__72C60C4A]  DEFAULT ((0)) FOR [Gold]
GO
ALTER TABLE [dbo].[Player_Info] ADD  CONSTRAINT [DF__Player_In__Creat__76969D2E]  DEFAULT (getdate()) FOR [CreateDate]
GO
ALTER TABLE [dbo].[User_Info] ADD  DEFAULT (getdate()) FOR [CreateDate]
GO
ALTER TABLE [dbo].[Player_Item]  WITH CHECK ADD  CONSTRAINT [FK_Player_Item_Player_Info] FOREIGN KEY([OwnerId])
REFERENCES [dbo].[Player_Info] ([PlayerId])
GO
ALTER TABLE [dbo].[Player_Item] CHECK CONSTRAINT [FK_Player_Item_Player_Info]
GO
ALTER TABLE [dbo].[Player_Item]  WITH CHECK ADD  CONSTRAINT [CK_Player_Item_Count] CHECK  (([Count]>=(1)))
GO
ALTER TABLE [dbo].[Player_Item]  WITH CHECK ADD  CONSTRAINT [CK_Player_Item_Location] CHECK  (([Location]>=(1) AND [Location]<=(4)))
GO
ALTER TABLE [dbo].[Player_Item]  WITH CHECK ADD  CONSTRAINT [CK_Player_Item_EquipSlot] CHECK  (([Location]=(2) AND [EquipSlot]>=(1) AND [EquipSlot]<=(7) OR [Location]<>(2) AND [EquipSlot]=(0)))
GO
ALTER TABLE [dbo].[Player_Item]  WITH CHECK ADD  CONSTRAINT [CK_Player_Item_BagSlot] CHECK  (([Location]=(1) AND [BagSlot]>=(0) OR [Location]<>(1) AND [BagSlot]=(-1)))
GO
ALTER TABLE [dbo].[Player_Info]  WITH CHECK ADD  CONSTRAINT [FK_Player_Info_User_Account] FOREIGN KEY([AccountId])
REFERENCES [dbo].[User_Account] ([AccountId])
GO
ALTER TABLE [dbo].[Player_Info] CHECK CONSTRAINT [FK_Player_Info_User_Account]
GO
ALTER TABLE [dbo].[User_Info]  WITH CHECK ADD  CONSTRAINT [FK_User_Info_User_Account] FOREIGN KEY([AccountId])
REFERENCES [dbo].[User_Account] ([AccountId])
ON DELETE CASCADE
GO
ALTER TABLE [dbo].[User_Info] CHECK CONSTRAINT [FK_User_Info_User_Account]
GO
ALTER TABLE [dbo].[Player_Info]  WITH CHECK ADD  CONSTRAINT [CK_Player_Info_Level] CHECK  (([Level]>=(1)))
GO
ALTER TABLE [dbo].[Player_Info]  WITH CHECK ADD  CONSTRAINT [CK_Player_Info_Exp] CHECK  (([Exp]>=(0)))
GO
ALTER TABLE [dbo].[Player_Info]  WITH CHECK ADD  CONSTRAINT [CK_Player_Info_Hp] CHECK  (([Hp]>=(0)))
GO
ALTER TABLE [dbo].[Player_Info]  WITH CHECK ADD  CONSTRAINT [CK_Player_Info_Gold] CHECK  (([Gold]>=(0)))
GO
/****** 개체: Table [dbo].[Schema_Version] — 적용한 마이그레이션(migrations/NNNN_*.sql) 1개 = 행 1개 ******/
CREATE TABLE [dbo].[Schema_Version](
	[Version] [int] NOT NULL,
	[Name] [nvarchar](100) NOT NULL,
	[AppliedAt] [datetime2](0) NOT NULL CONSTRAINT [DF_Schema_Version_AppliedAt] DEFAULT (sysdatetime()),
 CONSTRAINT [PK_Schema_Version] PRIMARY KEY CLUSTERED ([Version] ASC)
) ON [PRIMARY]
GO
-- 이 파일은 아래 버전까지의 변경을 이미 담고 있다. 마이그레이션을 추가하면 여기에도 행을 추가한다.
INSERT INTO [dbo].[Schema_Version] ([Version], [Name]) VALUES
	(1, N'0001_2026-10-28_drop_unused (schema.sql)'),
	(2, N'0002_2026-11-07_mc33_equip (schema.sql)'),
	(3, N'0003_2026-11-16_a25_schema_version (schema.sql)'),
	(4, N'0004_2026-11-16_a33_player_item (schema.sql)'),
	(5, N'0005_2026-11-16_a61_bag_slot (schema.sql)')
GO
USE [master]
GO
ALTER DATABASE [GameServer] SET  READ_WRITE 
GO
