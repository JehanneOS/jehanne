# Elliptic curve group operations in jacobian coordinates:
#	x=X/Z^2
#	x=Y/Z^3

jacobian_new(x,y,z, X,Y,Z) {
	X = x;
	Y = y;
	Z = z;
}
jacobian_inf(X,Y,Z) {
	X,Y,Z = jacobian_new(0,1,0);
}
jacobian_affine(p, X,Y,Z) mod(p) {
	if(Z != 0) {
		ZZ = Z^2;
		ZZZ = ZZ*Z;
		X = X / ZZ;
		Y = Y / ZZZ;
		Z = 1;
	}
}
jacobian_dbl(p,a, X1,Y1,Z1, X3,Y3,Z3) mod(p) {
	if(Y1 == 0) {
		X3,Y3,Z3 = jacobian_inf();
	} else {
		XX = X1^2;
		YY = Y1^2;
		YYYY = YY^2;
		ZZ = Z1^2;
		S = 2*((X1+YY)^2-XX-YYYY);
		M = 3*XX+a*ZZ^2;
		Z3 = (Y1+Z1)^2-YY-ZZ;	
		X3 = M^2-2*S;
		Y3 = M*(S-X3)-8*YYYY;
	}
}
jacobian_add(p,a, X1,Y1,Z1, X2,Y2,Z2, X3,Y3,Z3) mod(p) {
	Z1Z1 = Z1^2;
	Z2Z2 = Z2^2;
	U1 = X1*Z2Z2;
	U2 = X2*Z1Z1;
	S1 = Y1*Z2*Z2Z2;
	S2 = Y2*Z1*Z1Z1;
	if(U1 == U2) {
		if(S1 != S2) {
			X3,Y3,Z3 = jacobian_inf();
		} else {
			X3,Y3,Z3 = jacobian_dbl(p,a, X1,Y1,Z1);
		}
	} else {
		H = U2-U1;
		I = (2*H)^2;
		J = H*I;
		r = 2*(S2-S1);
		V = U1*I;
		X3 = r^2-J-2*V;
		Y3 = r*(V-X3)-2*S1*J;
		Z3 = ((Z1+Z2)^2-Z1Z1-Z2Z2)*H;
	}
}
