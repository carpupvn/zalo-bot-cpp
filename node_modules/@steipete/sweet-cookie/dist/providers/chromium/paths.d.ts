export type ResolvedCookiesDb = {
    dbPath: string;
    profile?: string;
    storeId?: string;
};
export declare const ALL_CHROMIUM_PROFILES: unique symbol;
export type ChromiumProfileSelector = string | typeof ALL_CHROMIUM_PROFILES;
type ResolveCookiesOptions = {
    profile?: ChromiumProfileSelector;
    roots: string[];
    cookieStoreOrder?: "legacy-first" | "network-first";
    onWarning?: (warning: string) => void;
};
export declare function looksLikePath(value: string): boolean;
export declare function expandPath(input: string): string;
export declare function safeStat(candidate: string): {
    isFile: () => boolean;
    isDirectory: () => boolean;
} | null;
export declare function resolveCookiesDbFromProfileOrRoots(options: ResolveCookiesOptions): string | null;
export declare function resolveCookiesDbsFromProfileOrRoots(options: ResolveCookiesOptions): ResolvedCookiesDb[];
export declare function profileNameFromDbPath(dbPath: string): string | undefined;
export declare function storeIdFromDbPath(dbPath: string): string;
export {};
//# sourceMappingURL=paths.d.ts.map